#pragma once

#include <nlohmann/json.hpp>

#include "ManualPlacementOverlay.h"

namespace rs::world {

nlohmann::json PlacedIndividualToJson(const PlacedIndividual& item);
PlacedIndividual PlacedIndividualFromJson(const nlohmann::json& json);
nlohmann::json WireSpanToJson(const WireSpan& span);
WireSpan WireSpanFromJson(const nlohmann::json& json);
nlohmann::json RunAnchorToJson(const RunAnchor& section);
RunAnchor RunAnchorFromJson(const nlohmann::json& json);
nlohmann::json OverlayToJson(const ManualPlacementOverlay& overlay);
ManualPlacementOverlay OverlayFromJson(const nlohmann::json& json);

}  // namespace rs::world
