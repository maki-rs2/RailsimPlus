#include "WorkspaceShell.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "OverlayCatalog.h"
#include "ScreenCatalog.h"
#include "WorkModeCatalog.h"
#include "../input/EscapeRouting.h"

namespace rs::app::workspace {

void WorkspaceShell::EnterWorkModeInternal(WorkMode mode) {
    screen_ = Screen::Work;
    workMode_ = mode;
    // 作業モードへ入ると(切替を含む)左右タブを開いた状態へ戻す。
    OpenAllTabs();
    ApplyScreenTransitionSideEffects();
}

void WorkspaceShell::ReturnToSpectate() {
    screen_ = Screen::Spectate;
    ApplyScreenTransitionSideEffects();
}

void WorkspaceShell::ApplyScreenTransitionSideEffects() {
    // 遷移に伴う処理: モード切替・鑑賞への復帰・環境画面/カメラ画面への
    // 移動はすべてプラグイン設定パネルを閉じる。
    ClosePluginSettings();

    // 敷設エディタは、それを持つ作業モードにいる間だけ開く。すでに開いていれば
    // 開いた順を変えない(あとから開くプラグイン設定パネルが手前に来る順を保つ)。
    const bool wantLayingEditor = IsWorkScreen(screen_) && GetWorkModeLayout(workMode_).hasLayingEditor;
    if (wantLayingEditor) {
        if (!IsOverlayOpen(OverlayKind::LayingEditor)) OpenOverlay(OverlayKind::LayingEditor);
    } else {
        CloseOverlay(OverlayKind::LayingEditor);
    }

    // ブラウザに付属する重ね表示(ルート管理ウィンドウ)は、左タブがブラウザの画面でだけ開いておく。
    const bool browserPresent = IsWorkScreen(screen_) &&
                                GetWorkModeLayout(workMode_).tabs[EnumIndex(NotchSide::Left)] == TabContent::PluginBrowser;
    if (!browserPresent) {
        for (const OverlayTraits& traits : kOverlayCatalog) {
            if (traits.requiresPluginBrowser) CloseOverlay(traits.kind);
        }
    }
}

bool WorkspaceShell::IsOverlayOpen(OverlayKind kind) const { return overlayOrder_[EnumIndex(kind)] != 0; }

void WorkspaceShell::OpenOverlay(OverlayKind kind) { overlayOrder_[EnumIndex(kind)] = ++openOrderCounter_; }

void WorkspaceShell::CloseOverlay(OverlayKind kind) { overlayOrder_[EnumIndex(kind)] = 0; }

void WorkspaceShell::SelectMode(WorkMode mode) {
    // 直接移れない画面(カメラ画面)からは作業モードへ移らない(鑑賞を経由する)。
    if (!GetScreenLayout(screen_).allowsDirectTransition) return;
    if (IsWorkScreen(screen_) && workMode_ == mode) {
        ReturnToSpectate();
        return;
    }
    EnterWorkModeInternal(mode);
}

void WorkspaceShell::EnterScreen(Screen screen) {
    if (!GetScreenLayout(screen_).allowsDirectTransition) return;
    screen_ = screen;
    OpenAllTabs();
    ApplyScreenTransitionSideEffects();
}

void WorkspaceShell::EnterEnvironment() {
    if (screen_ == Screen::Environment) Close();
    else EnterScreen(Screen::Environment);
}

void WorkspaceShell::EnterCamera() { EnterScreen(Screen::Camera); }

void WorkspaceShell::Close() {
    if (GetScreenLayout(screen_).closeReturnsToSpectate) ReturnToSpectate();
}

void WorkspaceShell::ToggleNotch(NotchSide side) {
    // 中身(パネル/プラグインブラウザ)によらず、そのタブの開閉を反転する。
    const std::size_t index = EnumIndex(side);
    tabOpen_[index] = !tabOpen_[index];
}

void WorkspaceShell::SelectRailTab(RailTab tab) {
    // 切り替えるだけ。Editorの選択の解除は、ToolModeの変化をTickEditorViewportが見て行う。
    railTab_ = tab;
}

void WorkspaceShell::OpenPluginSettings(const rs::world::Guid& targetEntityId) {
    // 要求のたびに開き直す(直前に×で閉じていても、開いていて別の対象でも、手前へ出す)。
    pluginSettingsTarget_ = targetEntityId;
    OpenOverlay(OverlayKind::PluginSettingsPanel);
}

void WorkspaceShell::ClosePluginSettings() { CloseOverlay(OverlayKind::PluginSettingsPanel); }

const rs::world::Guid& WorkspaceShell::PluginSettingsTarget() const {
    static const rs::world::Guid kEmpty = rs::world::NilGuid();
    return IsOverlayOpen(OverlayKind::PluginSettingsPanel) ? pluginSettingsTarget_ : kEmpty;
}

std::vector<OverlayKind> WorkspaceShell::OpenOverlaysBackToFront() const {
    std::vector<OverlayKind> open;
    for (const OverlayKind kind : magic_enum::enum_values<OverlayKind>()) {
        if (IsOverlayOpen(kind)) open.push_back(kind);
    }
    // 層が高いほど手前、同じ層では後から開いたほど手前。
    std::sort(open.begin(), open.end(), [this](OverlayKind a, OverlayKind b) {
        const int layerA = GetOverlayTraits(a).layer;
        const int layerB = GetOverlayTraits(b).layer;
        if (layerA != layerB) return layerA < layerB;
        return overlayOrder_[EnumIndex(a)] < overlayOrder_[EnumIndex(b)];
    });
    return open;
}

BackResult WorkspaceShell::Back(bool toolOperationPending) {
    BackResult result = BackResult::OpenedMenu;
    input::EscapeHandlers handlers{};
    handlers[EnumIndex(input::EscapeTarget::Overlay)] = [&] {
        const std::vector<OverlayKind> open = OpenOverlaysBackToFront();
        for (std::size_t i = open.size(); i > 0; --i) {
            if (!GetOverlayTraits(open[i - 1]).closableByEsc) continue;
            CloseOverlay(open[i - 1]);
            result = BackResult::ClosedOverlay;
            return true;
        }
        return false;
    };
    handlers[EnumIndex(input::EscapeTarget::Tool)] = [&] {
        if (!toolOperationPending) return false;
        result = BackResult::DeferredToTool;
        return true;
    };
    handlers[EnumIndex(input::EscapeTarget::LeaveCabinView)] = [&] {
        if (!cabinView_) return false;
        result = BackResult::LeaveCabinView;
        return true;
    };
    handlers[EnumIndex(input::EscapeTarget::ReturnToSpectate)] = [&] {
        if (screen_ == Screen::Spectate) return false;
        ReturnToSpectate();
        result = BackResult::ReturnedToSpectate;
        return true;
    };
    handlers[EnumIndex(input::EscapeTarget::MainMenu)] = [&] {
        OpenOverlay(OverlayKind::MainMenu);
        return true;
    };
    input::DispatchEscape(handlers);
    return result;
}

WorkspaceSnapshot WorkspaceShell::Snapshot() const {
    WorkspaceSnapshot snapshot;
    snapshot.screen = screen_;
    snapshot.workMode = workMode_;
    snapshot.railTab = railTab_;

    // 画面ごとの固定UIの表示(ScreenCatalog)のとおり。
    const ScreenLayout& screenLayout = GetScreenLayout(screen_);
    snapshot.bannerVisible = screenLayout.bannerVisible;
    snapshot.bottomBarVisible = screenLayout.bottomBarVisible;
    snapshot.screenBackTitle = screenLayout.backHeaderTitle;

    const bool inWork = IsWorkScreen(screen_);
    const WorkModeLayout& layout = GetWorkModeLayout(workMode_);
    for (const NotchSide side : magic_enum::enum_values<NotchSide>()) {
        const std::size_t index = EnumIndex(side);
        EdgePanelSnapshot& tab = snapshot.tabs[index];
        tab.content = inWork ? layout.tabs[index] : screenLayout.tabs[index];
        tab.open = tabOpen_[index];
    }

    // 運転台視点中は、画面の表にかかわらず固定UIをすべて消す。
    snapshot.cabinView = cabinView_;
    snapshot.signalPlacing = signalPlacing_;
    if (cabinView_) {
        snapshot.bannerVisible = false;
        snapshot.bottomBarVisible = false;
        for (EdgePanelSnapshot& tab : snapshot.tabs) tab.content = TabContent::None;
    }

    // 重なり順(z)はEscで閉じる順と同じ並び(OpenOverlaysBackToFront)から、奥から
    // 1,2,3...と振る。開いていないものは0。
    const std::vector<OverlayKind> open = OpenOverlaysBackToFront();
    for (std::size_t rank = 0; rank < open.size(); ++rank) {
        OverlaySnapshot& entry = snapshot.overlays[EnumIndex(open[rank])];
        entry.open = true;
        entry.z = static_cast<int>(rank) + 1;
    }
    // モーダル同士は一番手前の1つだけ見せる(ゲーム設定を重ねている間は主メニューを隠す)。
    // 操作を止める重ね表示(blocksInput)が見えているときも、モーダルと同じく幕を出して操作を止める。
    bool frontModalFound = false;
    bool inputBlocked = false;
    for (std::size_t i = open.size(); i > 0; --i) {
        const OverlayKind kind = open[i - 1];
        OverlaySnapshot& entry = snapshot.overlays[EnumIndex(kind)];
        if (!GetOverlayTraits(kind).modal) {
            entry.visible = true;
            inputBlocked = inputBlocked || GetOverlayTraits(kind).blocksInput;
        } else if (!frontModalFound) {
            entry.visible = true;
            frontModalFound = true;
        }
    }
    snapshot.modalVisible = frontModalFound || inputBlocked;

    return snapshot;
}

} // namespace rs::app::workspace
