#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "Guid.h"

namespace rs::world {

std::string GuidToHex(const Guid& guid);
std::optional<Guid> GuidFromHex(const std::string& hex);
nlohmann::json GuidToJson(const Guid& guid);
Guid GuidFromJson(const nlohmann::json& json, const Guid& fallback = NilGuid());

}  // namespace rs::world
