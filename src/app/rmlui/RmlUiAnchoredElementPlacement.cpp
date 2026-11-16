#include "RmlUiAnchoredElementPlacement.h"

#include <cmath>
#include <format>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

namespace rs::app::rmlui {

workspace::ClipPoint ProjectAnchoredPoint(const rs::core::Vec3& point, const rs::core::Mat4& viewProj) {
    // 軌道とカメラが持つ座標を計算用の精度へ広げてから、クリップ座標を求める。
    const double x = point.x;
    const double y = point.y;
    const double z = point.z;
    return {x * viewProj.m[0] + y * viewProj.m[4] + z * viewProj.m[8] + viewProj.m[12],
            x * viewProj.m[1] + y * viewProj.m[5] + z * viewProj.m[9] + viewProj.m[13],
            x * viewProj.m[3] + y * viewProj.m[7] + z * viewProj.m[11] + viewProj.m[15]};
}

AnchoredElementSizes MeasureAnchoredElementSizes(Rml::ElementDocument* document, const char* selector,
                                                const char* idAttribute) {
    AnchoredElementSizes measured;
    if (document == nullptr) return measured;
    Rml::ElementList elements;
    document->QuerySelectorAll(elements, selector);
    for (Rml::Element* element : elements) {
        if (element->GetOffsetWidth() <= 0.0f || element->GetOffsetHeight() <= 0.0f) continue;
        measured[element->GetAttribute<Rml::String>(idAttribute, Rml::String())] = {
            element->GetOffsetWidth(), element->GetOffsetHeight()};
    }
    return measured;
}

void PlaceMeasuredAnchoredElement(const AnchoredElementSizes& measured, const std::string& id,
                                  workspace::AnchoredElementPlacementInput input,
                                  Rml::String& left, Rml::String& top) {
    const AnchoredElementSizes::const_iterator found = measured.find(id);
    if (found != measured.end()) {
        input.elementWidth = found->second.width;
        input.elementHeight = found->second.height;
    }
    PlaceAnchoredElementPosition(workspace::PlaceAnchoredElement(input), left, top);
}

void PlaceAnchoredElementPosition(const workspace::AnchoredElementPlacement& placement,
                                  Rml::String& left, Rml::String& top, double verticalOffset) {
    left = std::format("{}px", std::lround(placement.left));
    top = std::format("{}px", std::lround(placement.top + verticalOffset));
}

} // namespace rs::app::rmlui
