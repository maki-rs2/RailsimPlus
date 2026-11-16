#pragma once

#include <array>
#include <functional>
#include <magic_enum/magic_enum.hpp>

#include "../workspace/CatalogCheck.h"
#include "../workspace/EnumIndex.h"

namespace rs::app::input {

enum class EscapeTarget {
    InputCapture, ConfirmDialog, ContextMenu, SignalPlacement, TextInput,
    TemplateSave, TimeControl, ScenePanel, DiaPanel, CameraScreen, BrowserFilter,
    Overlay, Tool, LeaveCabinView, ReturnToSpectate, MainMenu,
};

// イベントで消費されたEscはフレーム入力へ届かないため、両段階が同じ優先度を参照する。
inline constexpr EscapeTarget kEscapePriority[] = {
    EscapeTarget::InputCapture, EscapeTarget::ConfirmDialog, EscapeTarget::ContextMenu,
    EscapeTarget::SignalPlacement, EscapeTarget::TextInput,
    EscapeTarget::TemplateSave, EscapeTarget::TimeControl, EscapeTarget::ScenePanel, EscapeTarget::DiaPanel, EscapeTarget::CameraScreen,
    EscapeTarget::BrowserFilter, EscapeTarget::Overlay, EscapeTarget::Tool,
    EscapeTarget::LeaveCabinView, EscapeTarget::ReturnToSpectate, EscapeTarget::MainMenu,
};
static_assert(std::size(kEscapePriority) == magic_enum::enum_count<EscapeTarget>());
static_assert([] {
    for (std::size_t i = 0; i < std::size(kEscapePriority); ++i) {
        if (workspace::EnumIndex(kEscapePriority[i]) != i) return false;
    }
    return true;
}());

using EscapeHandlers = std::array<std::function<bool()>, std::size(kEscapePriority)>;

inline bool DispatchEscape(const EscapeHandlers& handlers) {
    for (const EscapeTarget target : kEscapePriority) {
        const std::function<bool()>& handler = handlers[workspace::EnumIndex(target)];
        if (handler && handler()) return true;
    }
    return false;
}

} // namespace rs::app::input
