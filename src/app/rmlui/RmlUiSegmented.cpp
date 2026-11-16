#include "RmlUiSegmented.h"

#include <cmath>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Property.h>
#include <RmlUi/Core/Tween.h>

#include "RmlUiElementQuery.h"

namespace rs::app::rmlui {

namespace {

// ハイライトがスライドする長さ(秒)。
// RCSSのトークン --anim-slide(tokens.rcss)と同じ値にする(C++からはRCSSの変数を読まないため、値を合わせて書く)。
constexpr float kIndicatorSlideSeconds = 0.15f;
// 未選択の区画を押している間、ハイライトをその方向へずらす量(dp)と、ずらす動きの長さ(秒)。
// トグルスイッチの押下時のずらしと揃える。量 = --press-shift(tokens.rcss)、
// 長さ = --anim-fast(tokens.rcss)。
constexpr float kIndicatorPressShiftDp = 3.f;
constexpr float kIndicatorPressSeconds = 0.05f;
// 目標とみなす差の許容値(px)。レイアウトの小数誤差で毎フレーム動き直さないため。
constexpr float kIndicatorEpsilonPx = 0.5f;

// ハイライトに覚えさせる目標値の属性名。アニメーション中は left/width が途中の値になるため、
// 目標が変わったかどうかは現在値ではなくこの属性と比べる。
constexpr const char* kTargetLeftAttribute = "data-indicator-left";
constexpr const char* kTargetWidthAttribute = "data-indicator-width";

// widgets.rcss側のクラス名・疑似クラス名。
constexpr const char* kSegmentedClass = "segmented";
constexpr const char* kSegmentClass = "segment";
constexpr const char* kIndicatorClass = "segment-indicator";
constexpr const char* kSelectedClass = "selected";
// 確認画面の見本で「押している状態」を固定表示するクラス。
constexpr const char* kStateActiveClass = "state-active";
constexpr const char* kActivePseudoClass = "active";

void SyncIndicator(Rml::Element& group) {
    Rml::Element* const indicator =
        FindChildIf(group, [](Rml::Element& child) { return child.IsClassSet(kIndicatorClass); });
    Rml::Element* const selected = FindChildIf(group, [](Rml::Element& child) {
        return child.IsClassSet(kSegmentClass) && child.IsClassSet(kSelectedClass);
    });
    if (indicator == nullptr || selected == nullptr) return;

    // 押している未選択の区画(確認画面の見本は .state-active クラス)。無効な帯では押しても動かさない。
    Rml::Element* pressed = nullptr;
    if (!group.HasAttribute("disabled")) {
        pressed = FindChildIf(group, [selected](Rml::Element& child) {
            if (!child.IsClassSet(kSegmentClass) || &child == selected) return false;
            return child.IsPseudoClassSet(kActivePseudoClass) || child.IsClassSet(kStateActiveClass);
        });
    }

    float targetLeft = selected->GetOffsetLeft();
    const float targetWidth = selected->GetOffsetWidth();
    float duration = kIndicatorSlideSeconds;
    if (pressed != nullptr) {
        const Rml::Context* const context = group.GetContext();
        const float dpRatio = (context != nullptr) ? context->GetDensityIndependentPixelRatio() : 1.f;
        const float direction = (pressed->GetOffsetLeft() > targetLeft) ? 1.f : -1.f;
        targetLeft += direction * kIndicatorPressShiftDp * dpRatio;
        duration = kIndicatorPressSeconds;
    }
    const bool placed = indicator->HasAttribute(kTargetLeftAttribute);
    if (placed) {
        const float lastLeft = indicator->GetAttribute<float>(kTargetLeftAttribute, 0.f);
        const float lastWidth = indicator->GetAttribute<float>(kTargetWidthAttribute, 0.f);
        if (std::fabs(lastLeft - targetLeft) < kIndicatorEpsilonPx &&
            std::fabs(lastWidth - targetWidth) < kIndicatorEpsilonPx)
            return;
    }
    indicator->SetAttribute(kTargetLeftAttribute, targetLeft);
    indicator->SetAttribute(kTargetWidthAttribute, targetWidth);

    const Rml::Property leftProperty(targetLeft, Rml::Unit::PX);
    const Rml::Property widthProperty(targetWidth, Rml::Unit::PX);
    if (!placed) {
        indicator->SetProperty(Rml::PropertyId::Left, leftProperty);
        indicator->SetProperty(Rml::PropertyId::Width, widthProperty);
        return;
    }
    const Rml::Tween tween(Rml::Tween::Cubic, Rml::Tween::Out);
    indicator->Animate(Rml::PropertyId::Left, leftProperty, duration, tween);
    indicator->Animate(Rml::PropertyId::Width, widthProperty, duration, tween);
}

} // namespace

void SyncSegmentIndicators(Rml::ElementDocument& document) {
    Rml::ElementList groups;
    document.GetElementsByClassName(groups, kSegmentedClass);
    for (Rml::Element* group : groups) SyncIndicator(*group);
}

} // namespace rs::app::rmlui
