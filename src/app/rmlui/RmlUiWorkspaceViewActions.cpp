#include "RmlUiWorkspaceView.h"

#include <functional>
#include <optional>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Types.h>

#include <spdlog/spdlog.h>

#include "RmlUiDataEventArgs.h"
#include "RmlUiEnumKey.h"
#include "../workspace/OverlayCatalog.h"

// RmlUiWorkspaceView.cpp から分けた、RMLのイベントとWorkspaceShellの操作の結線。
namespace rs::app::rmlui {

using rs::app::workspace::NotchSide;
using rs::app::workspace::OverlayKind;
using rs::app::workspace::RailTab;
using rs::app::workspace::WorkMode;

namespace {

// open_overlay/close_overlayの引数(重ね表示の名前)を種類へ引く。表に無い名前や、RMLのイベントで
// 開閉できない種別(userToggleableがfalse)は、警告を出して無視する(nulloptを返す)。
std::optional<OverlayKind> ResolveUserToggleableOverlay(const Rml::String& name) {
    const std::optional<OverlayKind> kind = rs::app::workspace::FindOverlayKindByName(name);
    if (!kind.has_value()) {
        spdlog::warn("[rmlui] 未知の重ね表示名です: {}", name);
        return std::nullopt;
    }
    if (!rs::app::workspace::GetOverlayTraits(*kind).userToggleable) {
        spdlog::warn("[rmlui] イベントから開閉できない重ね表示です: {}", name);
        return std::nullopt;
    }
    return kind;
}

} // namespace

void RmlUiWorkspaceView::BindShellAction(Rml::DataModelConstructor& constructor, const Rml::String& name,
                                         std::function<void(const Rml::String&)> action) {
    // 操作のあとは必ずRefreshViewModel()で表示へ反映する(操作ごとに書き忘れないよう、ここで包む)。
    constructor.BindEventCallback(
        name, [this, action](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            action(FirstEventArgument<Rml::String>(arguments, Rml::String()));
            RefreshViewModel();
        });
}

template <typename E>
void RmlUiWorkspaceView::BindEnumShellAction(Rml::DataModelConstructor& constructor, const Rml::String& name,
                                             std::function<void(E)> action) {
    BindShellAction(constructor, name, [name, action](const Rml::String& key) {
        const std::optional<E> value = ParseEnumKey<E>(key);
        if (!value.has_value()) {
            spdlog::warn("[rmlui] イベント{}の引数が未知です: {}", name, key);
            return;
        }
        action(*value);
    });
}

void RmlUiWorkspaceView::BindShellActions(Rml::DataModelConstructor& constructor) {
    rs::app::workspace::WorkspaceShell& shell = shell_;
    BindShellAction(constructor, "enter_environment", [&shell](const Rml::String&) { shell.EnterEnvironment(); });
    BindShellAction(constructor, "enter_camera", [&shell](const Rml::String&) { shell.EnterCamera(); });
    // 環境画面・カメラ画面共通の「閉じる」。
    BindShellAction(constructor, "close_screen", [&shell](const Rml::String&) { shell.Close(); });
    BindEnumShellAction<WorkMode>(constructor, "select_mode", [&shell](WorkMode mode) { shell.SelectMode(mode); });
    BindEnumShellAction<NotchSide>(constructor, "toggle_notch", [&shell](NotchSide side) { shell.ToggleNotch(side); });
    BindEnumShellAction<RailTab>(constructor, "select_rail_tab", [&shell](RailTab tab) { shell.SelectRailTab(tab); });
    // 重ね表示の開閉は名前(OverlayCatalogのbindName)を引数に取る2つのイベントにまとめる。
    BindShellAction(constructor, "open_overlay", [&shell](const Rml::String& name) {
        const std::optional<OverlayKind> kind = ResolveUserToggleableOverlay(name);
        if (kind.has_value()) shell.OpenOverlay(*kind);
    });
    BindShellAction(constructor, "close_overlay", [&shell](const Rml::String& name) {
        const std::optional<OverlayKind> kind = ResolveUserToggleableOverlay(name);
        if (kind.has_value()) shell.CloseOverlay(*kind);
    });
    // 開いていれば閉じ、閉じていれば開く(警告バッジのボタン)。
    BindShellAction(constructor, "toggle_overlay", [&shell](const Rml::String& name) {
        const std::optional<OverlayKind> kind = ResolveUserToggleableOverlay(name);
        if (!kind.has_value()) return;
        if (shell.Snapshot().Overlay(*kind).open) {
            shell.CloseOverlay(*kind);
        } else {
            shell.OpenOverlay(*kind);
        }
    });
    // 主メニューの項目はメニューを閉じて、押された項目を覚えるだけにする。
    const auto bindMenuCommand = [this, &constructor, &shell](const Rml::String& name, MainMenuCommand command) {
        BindShellAction(constructor, name, [this, &shell, command](const Rml::String&) {
            shell.CloseOverlay(OverlayKind::MainMenu);
            mainMenuCommand_ = command;
        });
    };
    bindMenuCommand("menu_save", MainMenuCommand::Save);
    bindMenuCommand("menu_save_as", MainMenuCommand::SaveAs);
    bindMenuCommand("menu_load", MainMenuCommand::Load);
    bindMenuCommand("menu_quit", MainMenuCommand::Quit);
}

} // namespace rs::app::rmlui
