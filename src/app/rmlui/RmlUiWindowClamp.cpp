#include "RmlUiWindowClamp.h"

#include <algorithm>
#include <cmath>

#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace rs::app::rmlui {

namespace {

constexpr const char* kHandleTag = "handle";
// はみ出しとみなす差の許容値(px)。レイアウトの小数誤差で毎フレーム書き換えないため
// (RmlUiSegmented.cpp の kIndicatorEpsilonPx と同じ考え方)。
constexpr float kClampEpsilonPx = 0.5f;

// 1軸分: 始点startと長さsizeを、0〜limitの中へ収めた始点を返す。sizeがlimitより大きければ0(左端・上端にそろえる)。
float ClampStart(float start, float size, float limit) {
    const float maxStart = std::max(0.f, limit - size);
    return std::clamp(start, 0.f, maxStart);
}

// left/topプロパティの今の値(px)。ElementHandle(Source/Core/ElementHandle.cpp)と同じく、
// 要素の位置から自分のmarginと親の枠線の幅を引いて求める(マージンで中央寄せした要素でも正しく戻せる)。
float CurrentPositionProperty(Rml::Element& window, Rml::BoxEdge edge) {
    const bool horizontal = (edge == Rml::BoxEdge::Left);
    const float offset = horizontal ? window.GetOffsetLeft() : window.GetOffsetTop();
    const float margin = window.GetBox().GetEdge(Rml::BoxArea::Margin, edge);
    Rml::Element* const parent = window.GetOffsetParent();
    const float parentBorder = (parent != nullptr) ? parent->GetBox().GetEdge(Rml::BoxArea::Border, edge) : 0.f;
    return offset - margin - parentBorder;
}

void ClampWindow(Rml::Element& window, float viewportWidth, float viewportHeight) {
    if (!window.IsVisible(true)) return;

    const float left = window.GetAbsoluteLeft();
    const float top = window.GetAbsoluteTop();
    const float clampedLeft = ClampStart(left, window.GetOffsetWidth(), viewportWidth);
    const float clampedTop = ClampStart(top, window.GetOffsetHeight(), viewportHeight);

    if (std::abs(clampedLeft - left) >= kClampEpsilonPx) {
        const float value = CurrentPositionProperty(window, Rml::BoxEdge::Left) + (clampedLeft - left);
        window.SetProperty(Rml::PropertyId::Left, Rml::Property(value, Rml::Unit::PX));
    }
    if (std::abs(clampedTop - top) >= kClampEpsilonPx) {
        const float value = CurrentPositionProperty(window, Rml::BoxEdge::Top) + (clampedTop - top);
        window.SetProperty(Rml::PropertyId::Top, Rml::Property(value, Rml::Unit::PX));
    }
}

} // namespace

void ClampMovableWindowsIntoView(Rml::ElementDocument& document) {
    const Rml::Context* const context = document.GetContext();
    if (context == nullptr) return;
    const Rml::Vector2i dimensions = context->GetDimensions();

    Rml::ElementList handles;
    document.GetElementsByTagName(handles, kHandleTag);
    for (Rml::Element* handle : handles) {
        // このアプリのウィンドウは move_target="#parent"(タイトルバーの親がウィンドウ本体)で作る。
        Rml::Element* const window = handle->GetParentNode();
        if (window == nullptr) continue;
        ClampWindow(*window, static_cast<float>(dimensions.x), static_cast<float>(dimensions.y));
    }
}

} // namespace rs::app::rmlui
