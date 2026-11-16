//
// ViewMode.h - 表示モード(レール/列車/情景/…/鑑賞)の列挙
//
// UIの実装に依存せず、入力と描画が同じモードの性質を参照できるようにする。
//
#pragma once

#include <array>
#include <magic_enum/magic_enum.hpp>

#include "../workspace/CatalogCheck.h"
#include "../workspace/EnumIndex.h"

namespace rs::app {

// 表示切り替えパネルで選択する表示モード。レール/列車/情景/…/ゲーム設定/描画設定/
// セーブデータのグループは排他的に1つだけ表示する(表示切り替えパネル自体は常駐)。
// Spectator(鑑賞モード)では表示切り替えパネル以外(プラグインパネル含む)を
// 全て隠す。
enum class ViewMode { Rail, Train, Scenery, StationDia, SignalBlock, GameSettings, SaveData, Spectator };

struct ViewModeTraits {
    ViewMode mode;
    const char* displayName;
    std::size_t switchOrder;
    bool trackEditing = false;
    bool train = false;
    bool scenery = false;
    bool stationDia = false;
    bool signalBlock = false;
    bool gameSettings = false;
    bool saveData = false;
    bool trackOverlay = false;
    bool arcOverlay = false;
    bool placementHighlight = false;
    bool sceneEntityOverlay = false;
    bool pluginInfo = true;
    bool clickOpensPluginSettings = false;
    bool cameraOperations = true;
    bool editContext = false;
    bool ownsBoxSelect = false;
    bool editorCommands = false;
};

inline constexpr ViewModeTraits kViewModeCatalog[] = {
    {.mode = ViewMode::Rail, .displayName = "レール", .switchOrder = 1, .trackEditing = true,
     .trackOverlay = true, .arcOverlay = true, .placementHighlight = true, .editContext = true, .editorCommands = true},
    {.mode = ViewMode::Train, .displayName = "列車", .switchOrder = 2, .train = true,
     .trackOverlay = true, .arcOverlay = true, .sceneEntityOverlay = true, .editContext = true},
    {.mode = ViewMode::Scenery, .displayName = "情景", .switchOrder = 3, .scenery = true,
     .placementHighlight = true, .sceneEntityOverlay = true, .editContext = true, .ownsBoxSelect = true, .editorCommands = true},
    {.mode = ViewMode::StationDia, .displayName = "ダイヤ", .switchOrder = 4, .stationDia = true,
     .trackOverlay = true, .editContext = true, .ownsBoxSelect = true},
    {.mode = ViewMode::SignalBlock, .displayName = "信号・閉塞", .switchOrder = 5, .signalBlock = true,
     .trackOverlay = true, .editContext = true, .ownsBoxSelect = true},
    {.mode = ViewMode::GameSettings, .displayName = "ゲーム設定", .switchOrder = 6, .gameSettings = true},
    {.mode = ViewMode::SaveData, .displayName = "セーブデータ", .switchOrder = 7, .saveData = true},
    {.mode = ViewMode::Spectator, .displayName = "鑑賞", .switchOrder = 0,
     .pluginInfo = false, .clickOpensPluginSettings = true},
};

static_assert(std::size(kViewModeCatalog) == magic_enum::enum_count<ViewMode>());
static_assert(workspace::IsCatalogInEnumOrder(kViewModeCatalog, &ViewModeTraits::mode));
static_assert([] {
    std::array<bool, std::size(kViewModeCatalog)> used{};
    for (const ViewModeTraits& traits : kViewModeCatalog) {
        if (traits.switchOrder >= used.size() || used[traits.switchOrder]) return false;
        used[traits.switchOrder] = true;
    }
    return true;
}());

constexpr const ViewModeTraits& GetViewModeTraits(ViewMode mode) {
    return kViewModeCatalog[workspace::EnumIndex(mode)];
}

} // namespace rs::app
