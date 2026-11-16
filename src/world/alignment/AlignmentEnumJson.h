#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "Alignment.h"

namespace rs::world {

inline const char* AlignmentKindToJson(AlignmentSegment::Kind kind) {
    switch (kind) {
    case AlignmentSegment::Kind::Line: return "line";
    case AlignmentSegment::Kind::Arc: return "arc";
    case AlignmentSegment::Kind::Transition: return "transition";
    }
    return "line";
}

inline AlignmentSegment::Kind AlignmentKindFromJson(
    const nlohmann::json& json,
    AlignmentSegment::Kind fallback = AlignmentSegment::Kind::Line) {
    if (!json.is_string()) return fallback;
    const std::string value = json.get<std::string>();
    if (value == "line") return AlignmentSegment::Kind::Line;
    if (value == "arc") return AlignmentSegment::Kind::Arc;
    if (value == "transition") return AlignmentSegment::Kind::Transition;
    return fallback;
}

inline const char* TransitionShapeToJson(AlignmentSegment::TransitionShape shape) {
    switch (shape) {
    case AlignmentSegment::TransitionShape::Clothoid: return "clothoid";
    case AlignmentSegment::TransitionShape::CubicParabola: return "cubicParabola";
    case AlignmentSegment::TransitionShape::SineHalf: return "sineHalf";
    }
    return "clothoid";
}

inline AlignmentSegment::TransitionShape TransitionShapeFromJson(
    const nlohmann::json& json,
    AlignmentSegment::TransitionShape fallback = AlignmentSegment::TransitionShape::Clothoid) {
    if (!json.is_string()) return fallback;
    const std::string value = json.get<std::string>();
    if (value == "clothoid") return AlignmentSegment::TransitionShape::Clothoid;
    if (value == "cubicParabola") return AlignmentSegment::TransitionShape::CubicParabola;
    if (value == "sineHalf") return AlignmentSegment::TransitionShape::SineHalf;
    return fallback;
}

}  // namespace rs::world
