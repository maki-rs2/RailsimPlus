//
// RmlUiLayingFields.cpp - 敷設エディタ(RmlUi版)の入力欄の表
//
#include "RmlUiLayingFields.h"

#include <array>
#include <cstddef>

#include "RmlUiNumberParse.h"
#include "../action/TrackLayingText.h"

namespace rs::app::rmlui {
namespace {

// 読んだ文字列を数として読み、setterへ渡す(読めなければfalse)。
template <typename Setter>
bool ApplyFloat(const std::string& text, Setter setter) {
    float value = 0.0f;
    if (!ParseFloatText(text, value)) return false;
    setter(value);
    return true;
}

template <typename Setter>
bool ApplyBool(const std::string& text, Setter setter) {
    if (text != "0" && text != "1") return false;
    setter(text == "1");
    return true;
}

const std::array<LayingField, 6> kLayingFields = {{
    {"layingAngleSnapDegrees", LayingFieldKind::Text, &LayingEditorModel::angleSnapDegrees,
     [](const TrackLayingViewModel& vm) { return FormatFixed(vm.angleSnapDegrees, kAngleSnapDecimals); },
     [](TrackLayingAction& action, const std::string& text) {
         return ApplyFloat(text, [&action](float v) { action.SetAngleSnapDegrees(v); });
     }},
    {"layingTrackCount", LayingFieldKind::Text, &LayingEditorModel::trackCount,
     [](const TrackLayingViewModel& vm) { return std::to_string(vm.trackCount); },
     [](TrackLayingAction& action, const std::string& text) {
         int value = 0;
         if (!ParseIntText(text, value)) return false;
         action.SetTrackCount(value);
         return true;
     }},
    {"layingTrackSpacing", LayingFieldKind::Text, &LayingEditorModel::trackSpacing,
     [](const TrackLayingViewModel& vm) { return FormatFixed(vm.trackSpacing, kTrackSpacingDecimals); },
     [](TrackLayingAction& action, const std::string& text) {
         return ApplyFloat(text, [&action](float v) { action.SetTrackSpacing(v); });
     }},
    {"layingTransitionLength", LayingFieldKind::Text, &LayingEditorModel::transitionLength,
     [](const TrackLayingViewModel& vm) { return FormatFixed(vm.transitionLength, kTransitionLengthDecimals); },
     [](TrackLayingAction& action, const std::string& text) {
         return ApplyFloat(text, [&action](float v) { action.SetTransitionLength(v); });
     }},
    {"layingTransition", LayingFieldKind::Combo, &LayingEditorModel::transition,
     [](const TrackLayingViewModel& vm) { return std::to_string(vm.transitionComboIndex); },
     [](TrackLayingAction& action, const std::string& text) {
         int index = 0;
         if (!ParseIntText(text, index) || index < 0 || index >= kTransitionChoiceCount) return false;
         const TransitionChoice& choice = TransitionChoiceAt(index);
         action.SetTransition(choice.enabled, choice.shape);
         return true;
     }},
    {kLayingAngleSnapEnabledKey, LayingFieldKind::Toggle, nullptr, nullptr,
     [](TrackLayingAction& action, const std::string& text) {
         return ApplyBool(text, [&action](bool v) { action.SetAngleSnapEnabled(v); });
     }},
}};

} // namespace

std::span<const LayingField> LayingFields() {
    return std::span<const LayingField>(kLayingFields);
}

const LayingField* FindLayingField(std::string_view key) {
    for (const LayingField& field : kLayingFields) {
        if (key == field.key) return &field;
    }
    return nullptr;
}

} // namespace rs::app::rmlui
