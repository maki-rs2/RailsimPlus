//
// CustomizerWindParsers.cpp - TrackWind / Windmill の読み込み(読むだけ)
//
#include "CustomizerParsers.h"

#include <algorithm>
#include <cmath>

namespace rs::legacy_pi {

namespace {

bool ReadNumber(const PiBlock& block, const char* key, double& out) {
    const PiAssignment* assignment = block.Find(key);
    if (assignment == nullptr || assignment->First() == nullptr || assignment->First()->kind != PiValue::Kind::Number)
        return false;
    out = assignment->First()->number;
    return true;
}

// 3値のタプルを読む。型や個数が違えばfalse。
bool ReadVec3(const PiBlock& block, const char* key, double (&out)[3]) {
    const PiAssignment* assignment = block.Find(key);
    if (assignment == nullptr || assignment->First() == nullptr) return false;
    const PiValue& value = *assignment->First();
    if (value.kind != PiValue::Kind::Tuple || value.tuple.size() != 3) return false;
    for (size_t i = 0; i < 3; ++i) {
        if (value.tuple[i].kind != PiValue::Kind::Number) return false;
        out[i] = value.tuple[i].number;
    }
    return true;
}

} // namespace

bool ParseWindTracker(const PiBlock& b, const TriggerInfo& trig, WindTrackerCustomizer& out) {
    WindTrackerCustomizer tracker;
    double trackSpeed = 0.0;
    if (!ReadNumber(b, "TrackSpeed", trackSpeed)) return false;
    tracker.trackSpeed = static_cast<float>(trackSpeed);
    if (b.Find("FixAxis") != nullptr) {
        double axis[3] = {};
        if (!ReadVec3(b, "FixAxis", axis)) return false;
        const double length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
        const double scale = length > 0.0 ? 1.0 / length : 0.0;
        tracker.hasFixAxis = true;
        for (size_t i = 0; i < 3; ++i) tracker.fixAxis[i] = static_cast<float>(axis[i] * scale);
    }
    tracker.trig = trig;
    out = std::move(tracker);
    return true;
}

bool ParseWindmill(const PiBlock& b, const TriggerInfo& trig, WindmillCustomizer& out) {
    WindmillCustomizer windmill;
    if (const PiAssignment* directional = b.Find("Directional")) {
        if (directional->First() == nullptr || directional->First()->kind != PiValue::Kind::Ident) return false;
        const PiValue& value = *directional->First();
        if (!value.IsYes() && !value.IsNo()) return false;
        windmill.directional = value.IsYes();
    }
    if (b.Find("RotationAxis") != nullptr) {
        double axis[3] = {};
        if (!ReadVec3(b, "RotationAxis", axis)) return false;
        for (size_t i = 0; i < 3; ++i) windmill.rotationAxis[i] = static_cast<float>(axis[i]);
    }
    double rotationSpeed = 0.0;
    double symmetric = 0.0;
    if (!ReadNumber(b, "RotationSpeed", rotationSpeed)) return false;
    if (!ReadNumber(b, "Symmetric", symmetric)) return false;
    windmill.rotationSpeedRps = static_cast<float>(rotationSpeed);
    windmill.symmetric = std::max(1, static_cast<int>(symmetric));
    windmill.trig = trig;
    out = std::move(windmill);
    return true;
}

} // namespace rs::legacy_pi
