#pragma once

#include <cstddef>

#include <nlohmann/json.hpp>

#include "WarpLinkTable.h"

namespace rs::world {

nlohmann::json WarpLinkTableToJson(const WarpLinkTable& table);
WarpLinkTable WarpLinkTableFromJson(const nlohmann::json& json, size_t sceneCount);

}  // namespace rs::world
