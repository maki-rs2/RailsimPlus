#include "RmlUiWorkspaceView.h"

#include <array>
#include <functional>
#include <optional>
#include <utility>

#include <windows.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Input.h>
#include <RmlUi/Core/Types.h>

#include <spdlog/spdlog.h>

#include "RmlUiDataEventArgs.h"
#include "RmlUiContextMenu.h"
#include "RmlUiKeyboardRouter.h"
#include "RmlUiEnumKey.h"
#include "../input/InputBindings.h"
#include "../input/InputSource.h"
#include "../workspace/EnumIndex.h"
#include "../workspace/GameSettingsPresentation.h"
#include "../workspace/OperationHintCatalog.h"
#include "../workspace/OverlayCatalog.h"
#include "../workspace/ScreenCatalog.h"
#include "../workspace/WorkModeCatalog.h"

namespace rs::app::rmlui {

using rs::app::workspace::EdgePanelSnapshot;
using rs::app::workspace::EnumIndex;
using rs::app::workspace::GetWorkModeLayout;
using rs::app::workspace::kWorkModeCatalog;
using rs::app::workspace::NotchSide;
using rs::app::workspace::kOverlayCatalog;
using rs::app::workspace::kOverlayKindCount;
using rs::app::workspace::OverlayKind;
using rs::app::workspace::OverlayTraits;
using rs::app::workspace::OverlaySnapshot;
using rs::app::workspace::RailTab;
using rs::app::workspace::WorkMode;
using rs::app::workspace::WorkspaceSnapshot;

// workspace.rmlの`data-model="workspace"`にバインドする表示専用の値の集まり。値は
// すべてFillFromSnapshot()がWorkspaceShell::Snapshot()とWorkModeCatalogから作り直す
// (状態の正典はWorkspaceShell側で、ここは次のRefreshViewModel()までの入れ物)。
struct WorkspaceViewModel {
    // 左右タブ1つ分。RML側は同じ部品(.edge-frame)にpresent/openをクラスとして結び、
    // contentで中身(パネル/プラグインブラウザ)を切り替える。
    struct EdgePanel {
        bool open = false;
        Rml::String content; // ToEnumKey(TabContent): "none" / "panel" / "pluginbrowser"
    };
    // 重ね表示1つ分(OverlayCatalogの全行)。
    struct Overlay {
        bool visible = false;
        int z = 0;
    };
    // 下部バーのモードボタン1個分。data-forでモード表の件数だけ生成する。
    // 操作説明の1項目。キー(枠で囲んで出す)と動作に分けて持つ。
    struct HintItem {
        Rml::String key;
        Rml::String action;
    };
    struct ModeButton {
        Rml::String key; // ToEnumKey(WorkMode)
        Rml::String icon;
        Rml::String tooltip;
        Rml::String label; // アイコンの下に出す小さい名前
        bool selected = false;
    };

    bool bannerVisible = false;
    // モーダルの重ね表示(主メニュー・ゲーム設定)が1つでも見えているか。見えている間はモーダル層
    // (暗くする幕と、その上のモーダルのウィンドウ)を出す。
    bool modalVisible = false;
    bool bottomBarVisible = false;
    bool cabinView = false; // 運転台視点中(戻るボタンを出す)
    Rml::String bannerTitle;
    Rml::String bannerIcon; // バナーのモード名の左に出すアイコン(下部バーのモードボタンと同じ素材)
    Rml::String operationHint; // 右下の操作説明(出さないときは空)
    Rml::String operationHintLabel; // 操作説明の先頭の札(配置待ちなどの状態名。出さないときは空)
    Rml::Vector<HintItem> operationHintItems; // 操作説明を項目(`キー:動作`)ごとに分けたもの。項目の間はRCSSで広く空ける

    EdgePanel leftTab;
    EdgePanel rightTab;

    Rml::String railTab; // ToEnumKey(RailTab): "lay" / "edit"

    // OverlayKindで添字付け。全行をRMLへバインドする。
    std::array<Overlay, kOverlayKindCount> overlays{};

    Rml::Vector<ModeButton> modeButtons;
};

namespace {

WorkspaceViewModel::EdgePanel ToEdgePanel(const EdgePanelSnapshot& panel) {
    WorkspaceViewModel::EdgePanel result;
    result.open = panel.open;
    result.content = ToEnumKey(panel.content);
    return result;
}

WorkspaceViewModel::Overlay ToOverlay(const OverlaySnapshot& overlay) {
    WorkspaceViewModel::Overlay result;
    result.visible = overlay.visible;
    result.z = overlay.z;
    return result;
}

void FillFromSnapshot(const WorkspaceSnapshot& snapshot, const std::vector<rs::app::input::InputBinding>& bindings,
                      WorkspaceViewModel& vm) {
    const bool inWork = snapshot.InWorkMode();

    vm.bannerVisible = snapshot.bannerVisible;
    vm.bottomBarVisible = snapshot.bottomBarVisible;
    vm.modalVisible = snapshot.modalVisible;
    vm.cabinView = snapshot.cabinView;
    vm.operationHint = rs::app::workspace::OperationHintFor(snapshot);
    vm.operationHintLabel = rs::app::workspace::OperationHintLabelFor(snapshot);
    vm.operationHintItems.clear();
    for (const rs::app::workspace::OperationHintItem& item : rs::app::workspace::SplitOperationHint(vm.operationHint)) {
        vm.operationHintItems.push_back({Rml::String(item.key), Rml::String(item.action)});
    }
    vm.bannerTitle = inWork ? Rml::String(GetWorkModeLayout(snapshot.workMode).displayName) : Rml::String();
    vm.bannerIcon = inWork ? Rml::String(GetWorkModeLayout(snapshot.workMode).bottomBarIcon) : Rml::String();

    vm.leftTab = ToEdgePanel(snapshot.Tab(NotchSide::Left));
    vm.rightTab = ToEdgePanel(snapshot.Tab(NotchSide::Right));

    vm.railTab = ToEnumKey(snapshot.railTab);

    for (const OverlayTraits& traits : kOverlayCatalog) {
        vm.overlays[EnumIndex(traits.kind)] = ToOverlay(snapshot.Overlay(traits.kind));
    }

    vm.modeButtons.clear();
    for (const rs::app::workspace::WorkModeLayout& entry : kWorkModeCatalog) {
        if (!entry.showInBottomBar) continue;
        WorkspaceViewModel::ModeButton button;
        button.key = ToEnumKey(entry.mode);
        button.icon = entry.bottomBarIcon;
        button.tooltip = entry.displayName;
        button.label = entry.displayName;
        if (entry.shortcutAction.has_value()) {
            const std::optional<rs::app::input::InputKey> key =
                rs::app::input::FirstInputKeyForAction(bindings, *entry.shortcutAction);
            if (key.has_value()) button.tooltip += " (" + rs::app::input::InputKeyToString(*key) + ")";
        }
        button.selected = inWork && (snapshot.workMode == entry.mode);
        vm.modeButtons.push_back(button);
    }
}

// 構造体をデータモデルへ登録する。
void RegisterViewModelTypes(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<WorkspaceViewModel::EdgePanel> handle =
            constructor.RegisterStruct<WorkspaceViewModel::EdgePanel>()) {
        handle.RegisterMember("open", &WorkspaceViewModel::EdgePanel::open);
        handle.RegisterMember("content", &WorkspaceViewModel::EdgePanel::content);
    }
    if (Rml::StructHandle<WorkspaceViewModel::Overlay> handle =
            constructor.RegisterStruct<WorkspaceViewModel::Overlay>()) {
        handle.RegisterMember("visible", &WorkspaceViewModel::Overlay::visible);
        handle.RegisterMember("z", &WorkspaceViewModel::Overlay::z);
    }
    if (Rml::StructHandle<WorkspaceViewModel::HintItem> handle = constructor.RegisterStruct<WorkspaceViewModel::HintItem>()) {
        handle.RegisterMember("key", &WorkspaceViewModel::HintItem::key);
        handle.RegisterMember("action", &WorkspaceViewModel::HintItem::action);
    }
    constructor.RegisterArray<Rml::Vector<WorkspaceViewModel::HintItem>>();
    if (Rml::StructHandle<WorkspaceViewModel::ModeButton> handle =
            constructor.RegisterStruct<WorkspaceViewModel::ModeButton>()) {
        handle.RegisterMember("key", &WorkspaceViewModel::ModeButton::key);
        handle.RegisterMember("icon", &WorkspaceViewModel::ModeButton::icon);
        handle.RegisterMember("tooltip", &WorkspaceViewModel::ModeButton::tooltip);
        handle.RegisterMember("label", &WorkspaceViewModel::ModeButton::label);
        handle.RegisterMember("selected", &WorkspaceViewModel::ModeButton::selected);
    }
    constructor.RegisterArray<Rml::Vector<WorkspaceViewModel::ModeButton>>();
    // 文字列の配列。複数の子ビュー(敷設エディタ・プラグイン設定パネル)が使うので、ここで1回だけ登録する。
    constructor.RegisterArray<Rml::Vector<Rml::String>>();
    // 並んだボタンの選択肢。ゲーム設定と日時の窓が使うので、ここで1回だけ登録する。
    if (Rml::StructHandle<GameSettingsChoice> handle = constructor.RegisterStruct<GameSettingsChoice>()) {
        handle.RegisterMember("key", &GameSettingsChoice::key);
        handle.RegisterMember("label", &GameSettingsChoice::label);
    }
    constructor.RegisterArray<Rml::Vector<GameSettingsChoice>>();
}

} // namespace

RmlUiWorkspaceView::RmlUiWorkspaceView()
    : toastQueue_(rs::app::ToastTuning::Embedded()), layingSet_(toastQueue_), gameSettings_(toastQueue_), renderSettings_(toastQueue_),
      templateSave_(toastQueue_), trainTab_(toastQueue_), signalPanel_(toastQueue_), diaPanel_(toastQueue_), scenePanel_(toastQueue_), cameraScreen_(toastQueue_),
      timeControl_(toastQueue_), helpWindow_(toastQueue_), snapshot_(shell_.Snapshot()),
      viewModel_(std::make_unique<WorkspaceViewModel>()) {}
RmlUiWorkspaceView::~RmlUiWorkspaceView() = default;

bool RmlUiWorkspaceView::CreateDataModel(Rml::Context& context) {
    Rml::DataModelConstructor constructor = context.CreateDataModel("workspace");
    if (!constructor) return false;

    RegisterViewModelTypes(constructor);
    constructor.Bind("bannerVisible", &viewModel_->bannerVisible);
    constructor.Bind("bottomBarVisible", &viewModel_->bottomBarVisible);
    constructor.Bind("cabinView", &viewModel_->cabinView);
    constructor.Bind("modalVisible", &viewModel_->modalVisible);
    constructor.Bind("bannerTitle", &viewModel_->bannerTitle);
    constructor.Bind("bannerIcon", &viewModel_->bannerIcon);
    constructor.Bind("operationHint", &viewModel_->operationHint);
    constructor.Bind("operationHintLabel", &viewModel_->operationHintLabel);
    constructor.Bind("operationHintItems", &viewModel_->operationHintItems);
    constructor.Bind("leftTab", &viewModel_->leftTab);
    constructor.Bind("rightTab", &viewModel_->rightTab);
    constructor.Bind("railTab", &viewModel_->railTab);
    for (const OverlayTraits& traits : kOverlayCatalog) {
        constructor.Bind(traits.bindName, &viewModel_->overlays[EnumIndex(traits.kind)]);
    }
    constructor.Bind("modeButtons", &viewModel_->modeButtons);
    BindShellActions(constructor);
    toastView_.RegisterModel(constructor);
    layingEditor_.RegisterModel(constructor);
    edgeEdit_.RegisterModel(constructor);
    layingSet_.RegisterModel(constructor);
    pluginBrowser_.RegisterModel(constructor);
    pluginRoots_.RegisterModel(constructor);
    missingPlugins_.RegisterModel(constructor);
    gameSettings_.RegisterModel(constructor);
    renderSettings_.RegisterModel(constructor);
    layoutFiles_.RegisterModel(constructor);
    recovery_.RegisterModel(constructor);
    loading_.RegisterModel(constructor);
    pluginSettings_.RegisterModel(constructor);
    sceneryYawStep_.RegisterModel(constructor);
    formationList_.RegisterModel(constructor);
    trainTab_.RegisterModel(constructor);
    signalPanel_.RegisterModel(constructor);
    templateSave_.RegisterModel(constructor, context);
    diaPanel_.RegisterModel(constructor);
    scenePanel_.RegisterModel(constructor);
    cameraScreen_.RegisterModel(constructor, context);
    timeControl_.RegisterModel(constructor);
    helpWindow_.RegisterModel(constructor);
    trainInfoCard_.RegisterModel(constructor);
    cabinHud_.RegisterModel(constructor);
    railLabel_.RegisterModel(constructor);
    diaCard_.RegisterModel(constructor);

    modelHandle_ = constructor.GetModelHandle();
    // 起動直後(鑑賞)の値を入れておく(文書の読込時の表示に使う)。
    RefreshViewModel();
    return true;
}

void RmlUiWorkspaceView::Tick(double deltaSeconds) {
    templateSave_.Refresh();
    diaPanel_.Refresh();
    scenePanel_.Refresh();
    cameraScreen_.Refresh(deltaSeconds);
    timeControl_.Refresh();
    toastQueue_.Advance(deltaSeconds);
    toastView_.Refresh(toastQueue_);
}

void RmlUiWorkspaceView::OpenOverlay(OverlayKind kind) {
    shell_.OpenOverlay(kind);
    RefreshViewModel();
}

void RmlUiWorkspaceView::CloseOverlay(OverlayKind kind) {
    shell_.CloseOverlay(kind);
    RefreshViewModel();
}

MainMenuCommand RmlUiWorkspaceView::TakeMainMenuCommand() {
    return std::exchange(mainMenuCommand_, MainMenuCommand::None);
}

rs::app::workspace::BackResult RmlUiWorkspaceView::Back(bool toolOperationPending) {
    const rs::app::workspace::BackResult result = shell_.Back(toolOperationPending);
    RefreshViewModel();
    return result;
}

void RmlUiWorkspaceView::SelectMode(WorkMode mode) {
    shell_.SelectMode(mode);
    RefreshViewModel();
}

void RmlUiWorkspaceView::ReturnToSpectate() {
    shell_.ReturnToSpectate();
    RefreshViewModel();
}

void RmlUiWorkspaceView::SetCabinView(bool active) {
    if (snapshot_.cabinView == active) return;
    shell_.SetCabinView(active);
    RefreshViewModel();
}

void RmlUiWorkspaceView::SetSignalPlacing(bool active) {
    if (snapshot_.signalPlacing == active) return;
    shell_.SetSignalPlacing(active);
    RefreshViewModel();
}

const rs::app::workspace::WorkspaceSnapshot& RmlUiWorkspaceView::Snapshot() const {
    return snapshot_;
}

void RmlUiWorkspaceView::OpenPluginSettings(const rs::world::Guid& targetEntityId) {
    shell_.OpenPluginSettings(targetEntityId);
    RefreshViewModel();
}

void RmlUiWorkspaceView::ClosePluginSettings() {
    shell_.ClosePluginSettings();
    RefreshViewModel();
}

const rs::world::Guid& RmlUiWorkspaceView::PluginSettingsTarget() const {
    return shell_.PluginSettingsTarget();
}

bool RmlUiWorkspaceView::IsModalOverlayVisible() const {
    return snapshot_.modalVisible;
}

void RmlUiWorkspaceView::AttachInput(const input::InputSource* input) {
    input_ = input;
    RefreshViewModel();
}

void RmlUiWorkspaceView::RefreshViewModel() {
    snapshot_ = shell_.Snapshot();
    if (modelHandle_) diaPanel_.SetActive(snapshot_.InWorkMode() && snapshot_.workMode == WorkMode::Dia);
    if (modelHandle_) {
        templateSave_.SetActive(snapshot_.InWorkMode() && snapshot_.workMode == WorkMode::Rail,
                                snapshot_.InWorkMode() && snapshot_.workMode == WorkMode::Train);
        scenePanel_.SetActive(workspace::GetScreenLayout(snapshot_.screen).hasScenePanel);
        cameraScreen_.SetActive(workspace::GetScreenLayout(snapshot_.screen).hasCameraScreen);
        timeControl_.SetVisible(snapshot_.bottomBarVisible);
        helpWindow_.Refresh(snapshot_);
    }
    static const std::vector<rs::app::input::InputBinding> kNoBindings;
    FillFromSnapshot(snapshot_, input_ != nullptr ? input_->Bindings() : kNoBindings, *viewModel_);
    if (modelHandle_) modelHandle_.DirtyAllVariables();
}

} // namespace rs::app::rmlui
