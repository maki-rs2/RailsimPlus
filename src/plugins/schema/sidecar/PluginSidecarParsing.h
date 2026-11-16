//
// PluginSidecarParsing.h - プラグイン拡張JSONの内部読込ヘルパ
//
#pragma once

#include <optional>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "../parser/Diagnostics.h"
#include "PluginSidecarCommon.h"

namespace rs::legacy_pi::sidecar {

constexpr int kSupportedSidecarVersion = 1;

std::optional<nlohmann::json> ReadSidecarJsonFile(
    const std::wstring& pluginFolder, const std::string& fileName,
    const std::string& diagContext, rs::legacy_pi::Diagnostics& diag);
SwitchAssignment ParseSwitchAssignment(const nlohmann::json& node,
                                       const std::string& diagContext,
                                       rs::legacy_pi::Diagnostics& diag);
SwitchValueTable ParseSwitchValueTable(const nlohmann::json& node,
                                       const std::string& diagContext,
                                       rs::legacy_pi::Diagnostics& diag);
DigitSwitchTable ParseDigitSwitchTable(const nlohmann::json& node,
                                       const std::string& diagContext,
                                       rs::legacy_pi::Diagnostics& diag);
SignalTable ParseSignalTable(const nlohmann::json& node,
                             const std::string& diagContext,
                             rs::legacy_pi::Diagnostics& diag);
SwitchMappingTable ParseSwitchMappingTable(const nlohmann::json& root,
                                           const std::set<std::string>& reservedTopLevelKeys,
                                           const std::string& diagContext,
                                           rs::legacy_pi::Diagnostics& diag);

} // namespace rs::legacy_pi::sidecar
