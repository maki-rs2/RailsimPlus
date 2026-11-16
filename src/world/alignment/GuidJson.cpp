#include "GuidJson.h"

#include <spdlog/spdlog.h>

namespace rs::world {
namespace {

int HexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

}  // namespace

std::string GuidToHex(const Guid& guid) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string result;
    result.reserve(guid.size() * 2);
    for (uint8_t byte : guid) {
        result.push_back(kHexDigits[byte >> 4]);
        result.push_back(kHexDigits[byte & 0x0f]);
    }
    return result;
}

std::optional<Guid> GuidFromHex(const std::string& hex) {
    if (hex.size() != 32) return std::nullopt;
    Guid result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        const int high = HexValue(hex[i * 2]);
        const int low = HexValue(hex[i * 2 + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        result[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return result;
}

nlohmann::json GuidToJson(const Guid& guid) {
    return GuidToHex(guid);
}

Guid GuidFromJson(const nlohmann::json& json, const Guid& fallback) {
    if (!json.is_string()) {
        spdlog::warn("[track_graph_serialize] GUIDが文字列ではないため既定値を使用します");
        return fallback;
    }
    const std::optional<Guid> guid = GuidFromHex(json.get<std::string>());
    if (!guid.has_value()) {
        spdlog::warn("[track_graph_serialize] GUID文字列が不正なため既定値を使用します");
        return fallback;
    }
    return *guid;
}

}  // namespace rs::world
