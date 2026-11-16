//
// CustomizerTrackingParsers.cpp - TrackingMove / TrackingRotation の読み込み(読むだけ)
//
#include "CustomizerParsers.h"

#include <numbers>

namespace rs::legacy_pi {

namespace {

// キーが書かれているときだけ実数として読む。書かれていなければ true で out は変えない。
// 書かれているのに実数でなければ false。
bool ReadOptionalNumber(const PiBlock& block, const char* key, double& out) {
    const PiAssignment* assignment = block.Find(key);
    if (assignment == nullptr) return true;
    if (assignment->First() == nullptr || assignment->First()->kind != PiValue::Kind::Number) return false;
    out = assignment->First()->number;
    return true;
}

// 3値のタプルを読む。書かれていなければ present=false で true。型や個数が違えば false。
bool ReadOptionalVec3(const PiBlock& block, const char* key, float (&out)[3], bool& present) {
    present = false;
    const PiAssignment* assignment = block.Find(key);
    if (assignment == nullptr) return true;
    if (assignment->First() == nullptr) return false;
    const PiValue& value = *assignment->First();
    if (value.kind != PiValue::Kind::Tuple || value.tuple.size() != 3) return false;
    for (size_t i = 0; i < 3; ++i)
        if (value.tuple[i].kind != PiValue::Kind::Number) return false;
    for (size_t i = 0; i < 3; ++i) out[i] = static_cast<float>(value.tuple[i].number);
    present = true;
    return true;
}

bool ReadTrackingId(const PiBlock& block, std::string& out) {
    const PiAssignment* assignment = block.Find("TrackingID");
    if (assignment == nullptr || assignment->First() == nullptr ||
        assignment->First()->kind != PiValue::Kind::String)
        return false;
    out = assignment->First()->text;
    return true;
}

// SliderRange は書かれたら半分にして持つ。省略は -1 のまま。
bool ReadSliderHalfRange(const PiBlock& block, float& out) {
    double slider = 0.0;
    if (block.Find("SliderRange") == nullptr) return true;
    if (!ReadOptionalNumber(block, "SliderRange", slider)) return false;
    out = static_cast<float>(slider * 0.5);
    return true;
}

} // namespace

bool ParseTrackingMove(const PiBlock& b, const TriggerInfo& trig, TrackingMoveCustomizer& out) {
    TrackingMoveCustomizer move;
    if (!ReadTrackingId(b, move.trackingId)) return false;
    if (b.Find("AscentRange") == nullptr) return false;
    double ascent = 0.0;
    if (!ReadOptionalNumber(b, "AscentRange", ascent)) return false;
    move.ascentRange = static_cast<float>(ascent);
    if (!ReadSliderHalfRange(b, move.sliderHalfRange)) return false;
    bool hasDisplacement = false;
    if (!ReadOptionalVec3(b, "Displacement", move.displacement, hasDisplacement) || !hasDisplacement) return false;
    ParseTiming(b, move.timing);
    move.trig = trig;
    out = std::move(move);
    return true;
}

bool ParseTrackingRotation(const PiBlock& b, const TriggerInfo& trig, TrackingRotationCustomizer& out) {
    TrackingRotationCustomizer rotation;
    if (!ReadTrackingId(b, rotation.trackingId)) return false;
    double radius = 0.0;
    double angleDeg = 0.0;
    if (!ReadOptionalNumber(b, "RotationRadius", radius)) return false;
    if (!ReadOptionalNumber(b, "RotationAngle", angleDeg)) return false;
    rotation.rotationRadius = static_cast<float>(radius);
    rotation.rotationAngleRad = static_cast<float>(angleDeg * std::numbers::pi / 180.0);
    if (!ReadSliderHalfRange(b, rotation.sliderHalfRange)) return false;
    bool hasAxis = false;
    if (!ReadOptionalVec3(b, "RotationAxis", rotation.rotationAxis, hasAxis)) return false;
    ParseTiming(b, rotation.timing);
    rotation.trig = trig;
    out = std::move(rotation);
    return true;
}

} // namespace rs::legacy_pi
