#include "RmlUiIntegration.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Input.h>

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>

#include <magic_enum/magic_enum.hpp>
#include <spdlog/spdlog.h>

#include "../../platform/file/AssetPath.h"
#include "../../platform/font/UiFont.h"
#include "../../world/alignment/Guid.h"
#include "../gpu/GpuContext.h"
#include "../workspace/CatalogCheck.h"
#include "../workspace/EnumIndex.h"
#include "RmlUiConfirmDialog.h"
#include "RmlUiOutsideClickCloser.h"
#include "RmlUiClickSound.h"
#include "RmlUiFieldCommit.h"
#include "../audio/UiSoundPlayer.h"
#include "RmlUiContextMenu.h"
#include "RmlUiDropDown.h"
#include "RmlUiKeyboardRouter.h"
#include "RmlUiUpdatePhase.h"
#include "RmlUiSegmented.h"
#include "RmlUiWindowClamp.h"

namespace rs::app::rmlui {

// ホストのフレームスロット数が、レンダラの配列の長さ(フェンス値・定数バッファ等)を超えないこと。
// 超えるとBeginFrame/SubmitFrameの添字が配列の外へ出る。
static_assert(kFrameCount <= RMLUI_RENDER_BACKEND_FIELD_SWAPCHAIN_BACKBUFFER_COUNT,
              "kFrameCountがレンダラのSWAPCHAIN_BACKBUFFER_COUNTを超えている");

namespace {

// RmlUiのコンテキスト名。文書の種類によらず同時に1個しか作らないため、共通の固定名にする。
constexpr const char* kContextName = "rmlui";

// ビューを作ってデータモデルを登録する(Rml::Context::LoadDocument()より前に呼ぶ)。失敗したらエラーを出してfalse。
template <typename View, typename... Args>
bool CreateViewWithDataModel(std::unique_ptr<View>& view, Rml::Context& context, const char* label, Args&&... args) {
    view = std::make_unique<View>(std::forward<Args>(args)...);
    if (view->CreateDataModel(context)) return true;
    spdlog::error("[rmlui] {}のデータモデル生成に失敗しました", label);
    return false;
}

} // namespace

RmlUiIntegration::RmlUiIntegration() = default;

RmlUiIntegration::~RmlUiIntegration() {
    if (context_ != nullptr) {
        spdlog::warn("[rmlui] RmlUiIntegration破棄時にShutdown()が未呼び出しです");
    }
}

bool RmlUiIntegration::Init(rs::app::GpuContext& gpu, HWND hwnd, UINT width, UINT height,
                            RmlUiDocumentKind kind, const std::string& themeName) {

    // SystemInterfaceを先に設定する(RenderInterface_DX12のコンストラクタ内でassert等が
    // Rml::Log::Message経由でSystemInterfaceを参照する可能性があるため、未設定のまま
    // 先に重い初期化を行わない)。
    systemInterface_ = std::make_unique<RmlUiSystemInterface>(hwnd);
    Rml::SetSystemInterface(systemInterface_.get());
    spdlog::debug("[rmlui] SystemInterface設定完了");

    // 角丸などの図形の縁を滑らかにするためMSAAを使う。サンプル数はCMakeLists.txtの
    // RMLUI_RENDER_BACKEND_FIELD_MSAA_SAMPLE_COUNT(EndFrame合成のコンパイル時分岐)と一致させる。
    renderInterface_ = std::make_unique<RenderInterface_DX12>(gpu.Device(), gpu.Queue(), gpu.CmdList(),
                                                                static_cast<unsigned char>(RMLUI_RENDER_BACKEND_FIELD_MSAA_SAMPLE_COUNT));
    spdlog::debug("[rmlui] RenderInterface_DX12構築完了");
    if (!static_cast<bool>(*renderInterface_)) {
        spdlog::error("[rmlui] RenderInterface_DX12の初期化に失敗しました");
        renderInterface_.reset();
        return false;
    }
    renderInterface_->SetViewport(static_cast<int>(width), static_cast<int>(height));
    spdlog::debug("[rmlui] SetViewport完了");

    Rml::SetRenderInterface(renderInterface_.get());
    Rml::SetTextInputHandler(&imeEditor_);

    if (!Rml::Initialise()) {
        spdlog::error("[rmlui] Rml::Initialise()に失敗しました");
        return false;
    }

    // 可変フォントは名前付きインスタンス(太さ)ごとに別の面として登録され、
    // RmlUi側が自動で全インスタンスを走査する
    // (RmlUi Source/Core/FontEngineDefault/FreeTypeInterface.cpp GetFaceVariations)。
    // そのためRCSSのfont-weight指定だけで太さを選べる。
    const std::string fontPath = rs::platform_font::ResolveUiFontPath();
    if (fontPath.empty() || !Rml::LoadFontFace(fontPath)) {
        spdlog::error("[rmlui] フォント読込に失敗しました: {}", rs::platform_font::kUiFontRelativePath);
    }

    // UIテーマ。文書の読込より先に
    // 生成し、UiThemePlugin経由でContext::LoadDocument内部の初回UpdateDocument()より前に
    // 適用する(UiThemePlugin宣言のコメント参照)。
    uiThemeSet_ = LoadEmbeddedUiTheme();
    themeRcss_ = BuildUiThemeRcss(uiThemeSet_);
    SetUiTheme(themeName); // Context生成前なので、名前を覚えるだけ(文書の読込時に有効になる)
    themePlugin_ = std::make_unique<UiThemePlugin>(*this);
    Rml::RegisterPlugin(themePlugin_.get());

    context_ = Rml::CreateContext(kContextName, Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)));
    if (context_ == nullptr) {
        spdlog::error("[rmlui] Rml::CreateContext()に失敗しました");
        return false;
    }

    // ビューとデータモデルはRml::Context::LoadDocument()より前に作る必要がある
    // (data-model属性の解決は文書読込時に行われるため)。文書の種類ごとの違いはDocumentSpecの表に集めている。
    const DocumentSpec& spec = GetDocumentSpec(kind);
    if (!(this->*spec.createView)()) return false;

    const std::string documentPath = rs::platform_file::ResolveAssetPath(spec.relativePath);
    if (documentPath.empty()) {
        spdlog::error("[rmlui] .rmlが見つかりません: {}", spec.relativePath);
        return false;
    }
    Rml::ElementDocument* document = context_->LoadDocument(documentPath);
    if (document == nullptr) {
        spdlog::error("[rmlui] .rmlの読込に失敗しました: {}", documentPath);
        return false;
    }
    document_ = document;

    document_->Show();
    contextMenu_ = std::make_unique<RmlUiContextMenu>(*document_);
    confirmDialog_ = std::make_unique<RmlUiConfirmDialog>(*document_);
    if (protoView_ != nullptr) protoView_->RegisterPopups(*contextMenu_, *confirmDialog_);
    if (workspaceView_ != nullptr) workspaceView_->RegisterPopups(*contextMenu_, *confirmDialog_);
    clickSound_ = std::make_unique<RmlUiClickSound>(*document_, [this](UiSound sound) {
        if (uiSound_ != nullptr) uiSound_->Push(sound);
    });
    fieldCommit_ = std::make_unique<RmlUiFieldCommit>(*document_);
    if (workspaceView_ != nullptr) {
        filterPanelCloser_ = std::make_unique<RmlUiOutsideClickCloser>(
            *document_, std::vector<std::string>{"browser-filter-panel", "browser-filter-button"},
            [this]() { workspaceView_->PluginBrowser().CloseFilterPanel(); });
        // 日時のウィンドウの外(時計のボタンは自分で開閉する)を押したら閉じる。
        timeWindowCloser_ = std::make_unique<RmlUiOutsideClickCloser>(
            *document_, std::vector<std::string>{"time-window", "time-clock"},
            [this]() { workspaceView_->TimeControl().CloseWindow(); });
        // プラグイン設定パネルの外(UIの別の場所)を押したら、選んでいるプラグインの選択を解除する(パネルを閉じる)。
        pluginSettingsCloser_ = std::make_unique<RmlUiOutsideClickCloser>(
            *document_, std::vector<std::string>{"float-window-pluginsettings"}, [this]() {
                if (!rs::world::GuidIsNil(workspaceView_->PluginSettingsTarget())) workspaceView_->ClosePluginSettings();
            });
    }
    keyboardRouter_ = std::make_unique<RmlUiKeyboardRouter>(*context_, *contextMenu_, *confirmDialog_,
        [this](ContextMenuKey key) { return workspaceView_ != nullptr && workspaceView_->HandleListKey(key, context_->GetHoverElement()); });

    spdlog::info("[rmlui] RmlUiIntegration初期化完了 ({}x{})", width, height);
    return true;
}

bool RmlUiIntegration::CreateWorkspaceView() {
    return CreateViewWithDataModel(workspaceView_, *context_, "ワークスペース");
}

bool RmlUiIntegration::CreateViewerView() {
    return CreateViewWithDataModel(viewerView_, *context_, "ビューアー");
}

bool RmlUiIntegration::CreateProtoView() {
    // 確認画面(proto.rml)のデータモデル(テーマ切替の帯など)。
    return CreateViewWithDataModel(protoView_, *context_, "確認画面", uiThemeSet_);
}

const RmlUiIntegration::DocumentSpec& RmlUiIntegration::GetDocumentSpec(RmlUiDocumentKind kind) {
    // RmlUiDocumentKindの列挙順に並べる(下のstatic_assertで確かめる)。
    static constexpr DocumentSpec kSpecs[] = {
        {RmlUiDocumentKind::Proto, "assets/ui/rmlui_proto/proto.rml", &RmlUiIntegration::CreateProtoView},
        {RmlUiDocumentKind::Workspace, "assets/ui/workspace/workspace.rml", &RmlUiIntegration::CreateWorkspaceView},
        {RmlUiDocumentKind::Viewer, "assets/ui/viewer/viewer.rml", &RmlUiIntegration::CreateViewerView},
    };
    static_assert(std::size(kSpecs) == magic_enum::enum_count<RmlUiDocumentKind>(),
                  "kSpecsの行数がRmlUiDocumentKindと一致しない");
    static_assert(rs::app::workspace::IsCatalogInEnumOrder(kSpecs, &DocumentSpec::kind),
                  "kSpecsの並びがRmlUiDocumentKindの列挙順と一致しない");
    return kSpecs[workspace::EnumIndex(kind)];
}

void RmlUiIntegration::SetTextureSourceResolver(RenderInterface_DX12::TextureSourceResolver resolver) {
    if (renderInterface_ != nullptr) renderInterface_->SetTextureSourceResolver(std::move(resolver));
}

RenderInterface_DX12::ResolvedTextureLoadStats RmlUiIntegration::TakeResolvedTextureLoadStats() {
    if (renderInterface_ == nullptr) return {};
    return renderInterface_->TakeResolvedTextureLoadStats();
}

void RmlUiIntegration::Shutdown() {
    // 呼び出し側が先に壊れる持ち物(画像の置き場)を引かないよう、引き先を外す。
    SetTextureSourceResolver({});
    // 文書に付けたリスナを外すため、文書(コンテキスト)より先に壊す。
    keyboardRouter_.reset();
    filterPanelCloser_.reset();
    pluginSettingsCloser_.reset();
    timeWindowCloser_.reset();
    clickSound_.reset();
    fieldCommit_.reset();
    confirmDialog_.reset();
    contextMenu_.reset();
    document_ = nullptr;
    if (context_ != nullptr) {
        Rml::RemoveContext(kContextName);
        context_ = nullptr;
    }
    // データモデルはworkspaceView_の変数とコールバックを参照しているため、
    // コンテキスト(とデータモデル)を破棄した後でビューを破棄する。
    workspaceView_.reset();
    viewerView_.reset();
    protoView_.reset();
    // Rml::Shutdown()はコンテキスト破棄に伴い残存テクスチャ/ジオメトリをRenderInterfaceへ
    // ReleaseTexture/ReleaseGeometryで返却する。RenderInterfaceのデストラクタより前に
    // 呼ぶ必要がある。
    Rml::SetTextInputHandler(nullptr);
    Rml::Shutdown();
    // Rml::Shutdown()は登録済みプラグインを自動でUnregisterPlugin+OnShutdown()するため
    // (Source/Core/PluginRegistry.cpp `PluginRegistry::NotifyShutdown`)、
    // themePlugin_の破棄はShutdown()呼び出しの後でよい。
    themePlugin_.reset();
    // ここに来る前に呼び出し側がGpuContext::WaitForGpu()済みであること
    // (RmlUiRenderInterface.h冒頭コメント差分。デストラクタは自前でFlushしない)。
    renderInterface_.reset();
    systemInterface_.reset();
}

void RmlUiIntegration::OnResize(UINT width, UINT height) {
    if (renderInterface_ == nullptr) return;
    renderInterface_->SetViewport(static_cast<int>(width), static_cast<int>(height));
    // GpuContext::OnResizeでスワップチェーンが作り直され、次のフレームスロットが0へ戻るため。
    renderInterface_->ResyncFrameSlots();
    if (context_ != nullptr) context_->SetDimensions(Rml::Vector2i(static_cast<int>(width), static_cast<int>(height)));
}

void RmlUiIntegration::NewFrame(float /*deltaSeconds*/) {
    if (context_ == nullptr) return;
    {
        // この間に出たchangeは、データ結線による書き込みや要素の作り直しから来たもの(ユーザーの操作ではない)。
        const ContextUpdateScope updateScope;
        context_->Update();
    }
    // セグメントボタンのハイライトは区画のレイアウト結果に合わせるため、Update()の後で合わせる
    // (ここで入れた値は次フレームのUpdate()で反映される)。
    if (document_ != nullptr) SyncSegmentIndicators(*document_);
    // コンボの選択肢の箱も同様に、開く向きがレイアウト後に決まるためUpdate()の後で判定する。
    if (document_ != nullptr) SyncDropDownReveal(*document_);
    // ドラッグで動かせるウィンドウも、アプリのウィンドウの大きさが変わったあとの位置をレイアウト後に確かめる。
    if (document_ != nullptr) ClampMovableWindowsIntoView(*document_);
    // メニューの位置も、大きさが決まるレイアウト後に置く。
    if (contextMenu_ != nullptr) contextMenu_->Sync(*context_);
}

void RmlUiIntegration::RecordFrame(UINT frameIndex, D3D12_CPU_DESCRIPTOR_HANDLE outputRtv) {
    if (context_ == nullptr || renderInterface_ == nullptr) return;
    outputRtv_ = outputRtv;
    renderInterface_->Set_UserRenderTarget(&outputRtv_);
    renderInterface_->BeginFrame(frameIndex);
    if (!uiHidden_) context_->Render();
    renderInterface_->EndFrame();
}

void RmlUiIntegration::SubmitFrame(UINT nextFrameIndex) {
    if (renderInterface_ == nullptr) return;
    renderInterface_->SubmitFrame(nextFrameIndex);
}

bool RmlUiIntegration::IsPointerOverUi() const {
    if (context_ == nullptr || uiHidden_) return false;
    return context_->IsMouseInteracting() || IsModalOverlayVisible() || IsConfirmDialogOpen();
}

bool RmlUiIntegration::IsModalOverlayVisible() const {
    return !uiHidden_ && workspaceView_ != nullptr && workspaceView_->IsModalOverlayVisible();
}

bool RmlUiIntegration::IsContextMenuOpen() const {
    return !uiHidden_ && contextMenu_ != nullptr && contextMenu_->IsOpen();
}

bool RmlUiIntegration::IsConfirmDialogOpen() const {
    return !uiHidden_ && confirmDialog_ != nullptr && confirmDialog_->IsOpen();
}

bool RmlUiIntegration::HasTextInputFocus() const {
    return !uiHidden_ && rmlui::HasTextInputFocus(context_);
}

} // namespace rs::app::rmlui
