#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace rs::app {

std::string DumpRslJson(const nlohmann::json& value);

}  // namespace rs::app
