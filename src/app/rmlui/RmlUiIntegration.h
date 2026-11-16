//
// RmlUiIntegration.h - Rml::Context の生成/破棄と、NewFrame/RecordFrame の
// オーケストレーション、Win32メッセージ(マウス・キーボード・IME)の受け口。
//
// 公式Win32_DX12バックエンドを使わず、
// 本体の既存DX12基盤(GpuContext/FrameRenderer)へRmlUiを差し込むための統合層。
// RmlUiの画面で起動したとき(AppCommandLine::UsesRmlUi・RmlUiDocumentKindFor参照)のみ生成される。
// --imguiのときは作らない。
//
#pragma once

#include <d3d12.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>

#include <RmlUi/Core/Plugin.h>

#include "RmlUiDocumentKind.h"
#include "RmlUiRenderInterface.h"
#include "RmlUiProtoView.h"
#include "RmlUiSystemInterface.h"
#include "RmlUiTheme.h"
#include "RmlUiViewerView.h"
#include "RmlUiWorkspaceView.h"

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace rs::app {
class GpuContext;
class UiSoundPlayer;
}

namespace rs::app::rmlui {

class RmlUiConfirmDialog;
class RmlUiOutsideClickCloser;
class RmlUiClickSound;
class RmlUiFieldCommit;
class RmlUiContextMenu;
class RmlUiKeyboardRouter;
enum class ContextMenuKey;

// RmlUiのbutton_index(Left=0/Right=1)。
inline constexpr int kMouseButtonLeft = 0;
inline constexpr int kMouseButtonRight = 1;

class RmlUiIntegration {
public:
    // contextMenu_の型が不完全なため、コンストラクタとデストラクタは.cppで定義する
    RmlUiIntegration();
    ~RmlUiIntegration();
    RmlUiIntegration(const RmlUiIntegration&) = delete;
    RmlUiIntegration& operator=(const RmlUiIntegration&) = delete;

    // gpu.Init()成功後に呼ぶこと(device/queue/コマンドリストが確定済みである必要がある)。
    // フォント(rs::platform_font::kUiFontRelativePath)と文書(kind別、
    // assets/ui/rmlui_proto/proto.rml またはassets/ui/workspace/workspace.rml)のパスは、
    // rs::platform_file::ResolveAssetPath(src/platform/file/AssetPath.h)で解決する。
    // themeNameは起動時に有効にするUIテーマ名(ユーザー設定の値。空や不明な名前はベーステーマ)。
    bool Init(rs::app::GpuContext& gpu, HWND hwnd, UINT width, UINT height, RmlUiDocumentKind kind,
              const std::string& themeName);

    // UIテーマの切替口(起動時のInitも、実行中の切替もここを通る)。空や不明な名前はベーステーマにする。
    // 読み込み済みの全文書へ反映される(Context::ActivateThemeが全文書のメディアクエリを更新する)。
    // これから読む文書にも、OnDocumentLoadで同じテーマを有効にする。
    void SetUiTheme(const std::string& themeName);
    const UiThemeSet& ThemeSet() const { return uiThemeSet_; }

    // テクスチャのソース名の引き先(RenderInterface_DX12::SetTextureSourceResolver)を登録する。
    // Init後に呼ぶこと。Shutdownで外すので、引き先の持ち主はそれより先に壊れてよい。
    void SetTextureSourceResolver(RenderInterface_DX12::TextureSourceResolver resolver);
    // ボタンのクリック音・決定音の要求の出し先(呼び出し側が所有し、この統合より長く生きること)。
    void SetUiSound(UiSoundPlayer* player) { uiSound_ = player; }

    // 引き先が扱ったテクスチャの作成にかかった時間と枚数(前回取り出してからの合計。取り出すと0へ戻る)。
    RenderInterface_DX12::ResolvedTextureLoadStats TakeResolvedTextureLoadStats();

    // gpu.WaitForGpu()済みの状態で呼ぶこと(GPU完全アイドルを前提に内部リソースを解放する、
    // RmlUiRenderInterface.h冒頭コメント参照)。
    void Shutdown();

    void OnResize(UINT width, UINT height);

    // レイアウト更新(Rml::Context::Update())。deltaSecondsは使わない。
    void NewFrame(float deltaSeconds);

    // 記録先RTVへRmlUiの描画コマンドを記録する(深度バッファなし)。呼び出し側
    // (App::Render)がFrameRenderer::RecordOverlay2Dの直後、ImGuiのRenderの直前に呼ぶこと。
    // 呼び出し前後でSRVディスクリプタヒープの切替/復帰は呼び出し側の責務
    // (RmlUiRenderInterfaceが描画のたびに自前のヒープへ切り替えるため、呼び出し後に
    // 共有ヒープへ戻す必要がある)。
    void RecordFrame(UINT frameIndex, D3D12_CPU_DESCRIPTOR_HANDLE outputRtv);

    // RecordFrame()を含むコマンドリストをホストがClose+ExecuteCommandLists(+Present)した
    // 直後に呼ぶこと。nextFrameIndexは次にRecordFrame()へ渡すフレームスロット番号。
    void SubmitFrame(UINT nextFrameIndex);

    // Win32メッセージをRmlUiへ転送する。戻り値true=このメッセージは消費済み
    // (呼び出し側はDefWindowProc等の追加処理を行わないこと)。マウスは「UIに当たって
    // いるか」(IsPointerOverUi())で振り分け、ボタンは押下時に決めた送り先を離しまで
    // 保持する。キーボードはテキスト入力欄へフォーカスしているときだけ消費する
    bool HandleWin32Message(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);


    // マウスカーソルがUI要素(pointer-events: auto の範囲)に当たっているか。
    // ImGuiInputBridge::PumpImGuiInput後のSetUiCapture合成に使う。
    // 主メニュー/ゲーム設定が見えている間もtrueにする(背後のビューポートを操作させない)。
    bool IsPointerOverUi() const;
    // UIをすべて隠している間は、描画せず、UIが入力も取らない。
    bool UiHidden() const { return uiHidden_; }
    void ToggleUiHidden() { uiHidden_ = !uiHidden_; }
    // 主メニューかゲーム設定が見えているか(ワークスペース文書以外ではfalse)。
    bool IsModalOverlayVisible() const;
    // フォーカス中の要素がテキスト入力(input[type=text系]・textarea)かどうか。
    // 同じくSetUiCapture合成、およびHandleWin32Messageのキーボード振り分けに使う。
    bool HasTextInputFocus() const;
    // コンテキストメニューが開いているか。開いている間は上下・Enter・Escをメニューが取るので、
    // SetUiCaptureのキーボード側へ合成する。
    bool IsContextMenuOpen() const;
    // 確認ダイアログが開いているか。開いている間は上下・Enter・Escをダイアログが取り、幕が全面のクリックを受ける。
    bool IsConfirmDialogOpen() const;
    // 確認ダイアログ。Init前やShutdown後はnullptr。
    RmlUiConfirmDialog* ConfirmDialog() { return confirmDialog_.get(); }

    // ワークスペース用ビュー。ワークスペース文書を読んでいない(Proto、または未初期化)ときはnullptr。
    RmlUiWorkspaceView* WorkspaceView() const { return workspaceView_.get(); }
    // プラグインビューアー用ビュー。ビューアー文書を読んでいないときはnullptr。
    RmlUiViewerView* ViewerView() const { return viewerView_.get(); }
    // 読み込み済みの文書(ワークスペースの毎フレームの更新でDOMの寸法を測るために使う)。無ければnullptr。
    Rml::ElementDocument* Document() const { return document_; }

private:
    bool uiHidden_ = false;
    // 文書が読み込まれるたび(Rml::Context::LoadDocument内、'load'イベント発火直前=
    // 初回UpdateDocument()より前)にUIテーマを適用する。ここで適用するのは、
    // UpdateDocument()より後だと文書の初回スタイル計算がテーマ変数未適用のまま走り、
    // `var(--*)`の"Invalid substitution"エラーが起動時に大量に出るため
    // (build/_deps/rmlui-src/Source/Core/Context.cpp `Context::LoadDocument(Stream*)`の
    // NotifyDocumentLoad→UpdateDocumentの順序を根拠に確認済み)。
    class UiThemePlugin : public Rml::Plugin {
    public:
        explicit UiThemePlugin(RmlUiIntegration& owner) : owner_(owner) {}
        int GetEventClasses() override { return EVT_DOCUMENT; }
        void OnDocumentLoad(Rml::ElementDocument* document) override;

    private:
        RmlUiIntegration& owner_;
    };

    std::unique_ptr<RenderInterface_DX12> renderInterface_;
    std::unique_ptr<RmlUiSystemInterface> systemInterface_;
    TextInputMethodEditorWin32 imeEditor_;
    Rml::Context* context_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    UiThemeSet uiThemeSet_;
    std::string themeRcss_;
    std::string activeTheme_; // 有効にしているテーマ名(解決済み)
    std::unique_ptr<UiThemePlugin> themePlugin_;
    std::unique_ptr<RmlUiProtoView> protoView_;
    std::unique_ptr<RmlUiWorkspaceView> workspaceView_;
    std::unique_ptr<RmlUiViewerView> viewerView_;
    std::unique_ptr<RmlUiContextMenu> contextMenu_;
    std::unique_ptr<RmlUiConfirmDialog> confirmDialog_;
    std::unique_ptr<RmlUiKeyboardRouter> keyboardRouter_;
    // 漏斗のパネルの外側を押したら閉じる(文書より先に破棄する)。
    std::unique_ptr<RmlUiOutsideClickCloser> filterPanelCloser_;
    std::unique_ptr<RmlUiOutsideClickCloser> pluginSettingsCloser_;
    std::unique_ptr<RmlUiOutsideClickCloser> timeWindowCloser_;
    std::unique_ptr<RmlUiClickSound> clickSound_;
    // 文字入力欄の Enter・Esc(文書より先に破棄する)。
    std::unique_ptr<RmlUiFieldCommit> fieldCommit_;
    D3D12_CPU_DESCRIPTOR_HANDLE outputRtv_{};

    // マウスボタン(RmlUiの button_index、Left=0/Right=1)ごとに押下時の送り先を
    // 離しまで保持する。
    // Consumed: メニューを閉じるために押されたボタン。離しも含めて、UIにもビューポートにも渡さない。
    enum class ButtonRouting { None, Ui, Viewport, Consumed };
    static constexpr std::size_t kMouseButtonCount = static_cast<std::size_t>(kMouseButtonRight) + 1; // 右の番号+1
    std::array<ButtonRouting, kMouseButtonCount> buttonRouting_{};
    UiSoundPlayer* uiSound_ = nullptr;
    // ビューポートで左ボタンを押した位置(クライアント座標)。離した位置との差が小さければクリックとみなす。
    int viewportPressX_ = 0;
    int viewportPressY_ = 0;
    // 編成一覧の上にポインタがあるときの上下キー。選択を移したらtrue(キーを消費する)。
    // いずれかのボタンの送り先がroutingか。
    bool AnyButtonRoutedTo(ButtonRouting routing) const;
    // 押下中のUI宛てのボタンを離した扱いにする(フォーカスやマウス捕捉を失い、離しが届かないとき)。
    void ReleaseHeldUiButtons();

    // 文書の種類ごとの違いを1行にまとめた表の行(表はGetDocumentSpec)。
    // createView: ビューとデータモデルの生成(LoadDocumentより前に呼ぶ。初期値もここで入る)。
    struct DocumentSpec {
        RmlUiDocumentKind kind;
        const char* relativePath;
        bool (RmlUiIntegration::*createView)();
    };
    static const DocumentSpec& GetDocumentSpec(RmlUiDocumentKind kind);
    bool CreateWorkspaceView();
    bool CreateProtoView();
    bool CreateViewerView();
};

} // namespace rs::app::rmlui
