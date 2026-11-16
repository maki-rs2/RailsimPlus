#include "RmlUiAnchoredArea.h"

#include <algorithm>

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StyleTypes.h>

namespace rs::app::rmlui {

namespace {

// workspace.rml・workspace.rcssのクラス名。
constexpr const char* kBannerSelector = ".banner";
constexpr const char* kBottomBarSelector = ".bottom-bar";
constexpr const char* kBottomBarBumpSelector = ".bottom-bar-bump";
constexpr const char* kEdgeLeftSelector = ".edge-left";
constexpr const char* kEdgeRightSelector = ".edge-right";
constexpr const char* kEdgeNotchSelector = ".edge-notch";
constexpr const char* kOpenClass = "is-open";
constexpr const char* kAbsentClass = "is-absent";

// data-ifで隠れている要素はdisplay: noneになる。
bool IsDisplayed(Rml::Element* element) {
    return element != nullptr && element->GetComputedValues().display() != Rml::Style::Display::None;
}

// タブが画面の端から占める幅。開いていれば本体とノッチ、閉じていればノッチ、無なら0。
double EdgeOccupiedWidth(Rml::Element& frame) {
    if (frame.IsClassSet(kAbsentClass)) return 0.0;
    Rml::Element* const notch = frame.QuerySelector(kEdgeNotchSelector);
    const double notchWidth = notch != nullptr ? notch->GetOffsetWidth() : 0.0;
    return frame.IsClassSet(kOpenClass) ? frame.GetOffsetWidth() + notchWidth : notchWidth;
}

} // namespace

AnchoredArea MeasureAnchoredArea(Rml::ElementDocument* document, double screenWidth, double screenHeight) {
    AnchoredArea area{0.0, 0.0, screenWidth, screenHeight};
    if (document == nullptr) return area;

    if (Rml::Element* const left = document->QuerySelector(kEdgeLeftSelector)) {
        area.left = std::max(area.left, EdgeOccupiedWidth(*left));
    }
    if (Rml::Element* const right = document->QuerySelector(kEdgeRightSelector)) {
        area.right = std::min(area.right, screenWidth - EdgeOccupiedWidth(*right));
    }
    Rml::Element* const banner = document->QuerySelector(kBannerSelector);
    if (IsDisplayed(banner)) {
        const double top = banner->GetAbsoluteTop();
        area.top = std::max(area.top, top + banner->GetOffsetHeight());
    }
    Rml::Element* const bottomBar = document->QuerySelector(kBottomBarSelector);
    if (IsDisplayed(bottomBar)) {
        area.bottom = std::min(area.bottom, static_cast<double>(bottomBar->GetAbsoluteTop()));
        Rml::Element* const bump = document->QuerySelector(kBottomBarBumpSelector);
        if (IsDisplayed(bump)) area.bottom = std::min(area.bottom, static_cast<double>(bump->GetAbsoluteTop()));
    }
    return area;
}

} // namespace rs::app::rmlui
