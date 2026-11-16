#include "RslJsonFieldRounding.h"

#include <array>
#include <cmath>
#include <optional>
#include <string_view>

namespace rs::app {
namespace {

struct FieldDigits {
    std::string_view key;
    int digits;
};

// レイアウトファイル(.rsl)の整形規約に対応する丸め桁表。値は調整パラメータであり、
// 意味の割り当て（どのフィールドに何桁与えるか）は設計側で確定済み。
constexpr std::array<FieldDigits, 38> kFieldDigitsTable = {{
    {"pos", 4},
    {"s", 4},
    {"sStart", 4},
    {"length", 4},
    {"edgeLength", 4},
    {"distance", 4},
    {"target", 4},
    {"lateralOffset", 4},
    {"verticalOffset", 4},
    {"anchorS", 4},
    {"stopOffset", 4},
    {"trackSpacing", 4},
    {"transitionLength", 4},

    {"tangent", 6},
    {"up", 6},

    {"cant", 3},
    {"cantA", 3},
    {"cantB", 3},
    {"cantMid", 3},
    {"cantAutoMax", 3},
    {"radius", 3},
    {"radiusEnd", 3},
    {"speed", 3},
    {"speedLimit", 3},
    {"targetSpeed", 3},
    {"stopDistance", 3},
    {"yaw", 3},
    {"pitch", 3},
    {"roll", 3},
    {"yawOffset", 3},
    {"fieldOfView", 3},
    {"angleSnap", 3},
    {"timeScale", 3},
    {"secondsOfDay", 3},
    {"phaseElapsed", 3},
    {"departureAt", 3},
    {"doorCloseAt", 3},

    {"grade", 5},
}};

std::optional<int> FindRoundingDigits(const std::string& key) {
    for (const FieldDigits& entry : kFieldDigitsTable) {
        if (entry.key == key) {
            return entry.digits;
        }
    }
    return std::nullopt;
}

double RoundToDigits(double value, int digits) {
    const double scale = std::pow(10.0, digits);
    return std::round(value * scale) / scale;
}

nlohmann::json RoundNode(const nlohmann::json& value, std::optional<int> digits) {
    if (value.is_number_float() && digits.has_value()) {
        return RoundToDigits(value.get<double>(), *digits);
    }
    if (value.is_array()) {
        nlohmann::json result = nlohmann::json::array();
        for (const nlohmann::json& item : value) {
            result.push_back(RoundNode(item, digits));
        }
        return result;
    }
    if (value.is_object()) {
        nlohmann::json result = nlohmann::json::object();
        for (nlohmann::json::const_iterator it = value.cbegin(); it != value.cend(); ++it) {
            const std::optional<int> childDigits = FindRoundingDigits(it.key());
            result[it.key()] = RoundNode(it.value(), childDigits);
        }
        return result;
    }
    return value;
}

}  // namespace

nlohmann::json RoundRslJsonFields(const nlohmann::json& value) {
    return RoundNode(value, std::nullopt);
}

}  // namespace rs::app
