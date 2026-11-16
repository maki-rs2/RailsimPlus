#pragma once

#include <string>
#include <unordered_map>

#include <RmlUi/Core/Types.h>

#include "../../core/Math.h"
#include "../workspace/AnchoredElementPlacement.h"

namespace Rml {
class ElementDocument;
}

namespace rs::app::rmlui {

struct AnchoredElementSize {
    double width = 0.0;
    double height = 0.0;
};

using AnchoredElementSizes = std::unordered_map<std::string, AnchoredElementSize>;

workspace::ClipPoint ProjectAnchoredPoint(const rs::core::Vec3& point, const rs::core::Mat4& viewProj);

// 前フレームでレイアウトされた要素の大きさを、対象のIDごとに読む。
AnchoredElementSizes MeasureAnchoredElementSizes(Rml::ElementDocument* document, const char* selector,
                                                const char* idAttribute);

// 全体の配置を求め、縦に積む場合はその内側のずれを足してデータモデルへ渡す。
void PlaceAnchoredElementPosition(const workspace::AnchoredElementPlacement& placement,
                                  Rml::String& left, Rml::String& top, double verticalOffset = 0.0);

// 未表示の要素はinputの大きさで置き、レイアウト済みなら測定値を使う。
void PlaceMeasuredAnchoredElement(const AnchoredElementSizes& measured, const std::string& id,
                                  workspace::AnchoredElementPlacementInput input,
                                  Rml::String& left, Rml::String& top);

} // namespace rs::app::rmlui
