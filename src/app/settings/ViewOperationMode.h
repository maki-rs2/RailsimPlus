//
// ViewOperationMode.h - 俯瞰カメラの視点操作の種類と、種類ごとの違いを持つ表
//
// 選ぶ値はユーザー設定(settings.dat の viewOperation)。画面の選択肢の並びはこの表と同じ。
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rs::app {

enum class ViewOperationMode : std::uint8_t {
    Wasd, // WASDで注目点を動かす(今の操作)
    Drag, // RailSim II と同じく、ドラッグで注目点を動かす(WASDも使える)
    Count,
};

// keyは保存する文字列、labelKeyは画面の文言の翻訳キー。
struct ViewOperationChoice {
    ViewOperationMode mode;
    const char* key;
    const char* labelKey;
};

inline constexpr std::array<ViewOperationChoice, static_cast<std::size_t>(ViewOperationMode::Count)>
    kViewOperationChoices = {{
        {ViewOperationMode::Wasd, "wasd", "game_settings.mouse_operation.wasd"},
        {ViewOperationMode::Drag, "drag", "game_settings.mouse_operation.drag"},
    }};

inline constexpr ViewOperationMode kDefaultViewOperationMode = ViewOperationMode::Wasd;

// 保存した文字列を種類へ変える。読めない文字列は既定。
constexpr ViewOperationMode ViewOperationModeFromKey(std::string_view key) {
    for (const ViewOperationChoice& choice : kViewOperationChoices) {
        if (key == choice.key) return choice.mode;
    }
    return kDefaultViewOperationMode;
}

constexpr const char* ViewOperationModeKey(ViewOperationMode mode) {
    return kViewOperationChoices[static_cast<std::size_t>(mode)].key;
}

} // namespace rs::app
