//
// PluginSidecarParsing.cpp - プラグイン拡張JSONの共通読込処理
//
#include "PluginSidecarParsing.h"

#include <filesystem>
#include <optional>
#include <utility>

#include "../../../platform/file/FileRead.h"
#include "../../load/PluginPaths.h"

namespace rs::legacy_pi::sidecar {
namespace {

void Warn(rs::legacy_pi::Diagnostics& diag, const std::string& diagContext,
          const std::string& message) {
    diag.Warn(0, 0, diagContext + ": " + message);
}

bool IsSystemSwitchLabel(const std::string& label) {
    return !label.empty() && label.front() == '_';
}

bool IsInteger(const nlohmann::json& node) {
    return node.is_number_integer();
}

} // namespace

std::optional<nlohmann::json> ReadSidecarJsonFile(
    const std::wstring& pluginFolder, const std::string& fileName,
    const std::string& diagContext, rs::legacy_pi::Diagnostics& diag) {
    const std::wstring path = rs::legacy_plugin::ResolvePath(pluginFolder, fileName);
    if (!rs::legacy_plugin::FileExists(path)) return std::nullopt;

    std::optional<std::string> loaded = rs::platform_file::ReadFileText(std::filesystem::path(path));
    if (!loaded) {
        Warn(diag, diagContext, fileName + " を開けないため破棄しました");
        return std::nullopt;
    }
    std::string text = std::move(*loaded);
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
        static_cast<unsigned char>(text[1]) == 0xbb &&
        static_cast<unsigned char>(text[2]) == 0xbf) {
        text.erase(0, 3);
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error&) {
        Warn(diag, diagContext, fileName + " のJSON解析に失敗したため破棄しました");
        return std::nullopt;
    }
    if (!root.is_object()) {
        Warn(diag, diagContext, fileName + " のトップレベルがobjectではないため破棄しました");
        return std::nullopt;
    }
    if (!root.contains("version") || !IsInteger(root["version"])) {
        Warn(diag, diagContext, fileName + " のversionがint型でないため破棄しました");
        return std::nullopt;
    }
    if (root["version"].get<int>() != kSupportedSidecarVersion) {
        Warn(diag, diagContext, fileName + " のversionが未対応のため破棄しました");
        return std::nullopt;
    }
    return root;
}

SwitchAssignment ParseSwitchAssignment(const nlohmann::json& node,
                                       const std::string& diagContext,
                                       rs::legacy_pi::Diagnostics& diag) {
    SwitchAssignment assignment;
    if (!node.is_object()) {
        Warn(diag, diagContext, "スイッチ割り当てがobjectではないため無視しました");
        return assignment;
    }
    for (const auto& item : node.items()) {
        const std::string& label = item.key();
        if (IsSystemSwitchLabel(label)) {
            Warn(diag, diagContext, "_始まりのスイッチラベル '" + label + "' を無視しました");
            continue;
        }
        if (!IsInteger(item.value())) {
            Warn(diag, diagContext, "スイッチラベル '" + label + "' の値がint型でないため無視しました");
            continue;
        }
        assignment.emplace(label, item.value().get<int>());
    }
    return assignment;
}

SwitchValueTable ParseSwitchValueTable(const nlohmann::json& node,
                                       const std::string& diagContext,
                                       rs::legacy_pi::Diagnostics& diag) {
    SwitchValueTable table;
    if (!node.is_object()) {
        Warn(diag, diagContext, "値別スイッチ対応表がobjectではないため無視しました");
        return table;
    }
    for (const auto& item : node.items()) {
        if (!item.value().is_object()) {
            Warn(diag, diagContext, "値 '" + item.key() + "' のスイッチ割り当てがobjectではないため無視しました");
            continue;
        }
        table.entries.emplace(item.key(), ParseSwitchAssignment(item.value(), diagContext, diag));
    }
    return table;
}

DigitSwitchTable ParseDigitSwitchTable(const nlohmann::json& node,
                                       const std::string& diagContext,
                                       rs::legacy_pi::Diagnostics& diag) {
    DigitSwitchTable table;
    if (!node.is_array()) {
        Warn(diag, diagContext, "桁別スイッチ対応表がarrayではないため無視しました");
        return table;
    }
    for (const nlohmann::json& item : node) {
        if (!item.is_array() || item.size() != 2 || !item[0].is_string() || !item[1].is_object()) {
            Warn(diag, diagContext, "桁別スイッチ要素の形式が不正なため無視しました");
            continue;
        }
        const std::string label = item[0].get<std::string>();
        if (IsSystemSwitchLabel(label)) {
            Warn(diag, diagContext, "_始まりの桁別スイッチラベル '" + label + "' を無視しました");
            continue;
        }
        DigitSwitchDigit digit;
        digit.switchLabel = label;
        for (const auto& mapping : item[1].items()) {
            if (!IsInteger(mapping.value())) {
                Warn(diag, diagContext, "桁別文字 '" + mapping.key() + "' の値がint型でないため無視しました");
                continue;
            }
            digit.charToCase.emplace(mapping.key(), mapping.value().get<int>());
        }
        table.push_back(std::move(digit));
    }
    return table;
}

SignalTable ParseSignalTable(const nlohmann::json& node,
                             const std::string& diagContext,
                             rs::legacy_pi::Diagnostics& diag) {
    SignalTable table;
    if (!node.is_array()) {
        Warn(diag, diagContext, "signalがarrayではないため無視しました");
        return table;
    }
    for (const nlohmann::json& item : node) {
        if (!item.is_array() || item.size() != 2 || !item[0].is_number() || !item[1].is_object()) {
            Warn(diag, diagContext, "signal要素の形式が不正なため無視しました");
            continue;
        }
        SignalStage stage;
        stage.speedKmh = item[0].get<double>();
        stage.assignment = ParseSwitchAssignment(item[1], diagContext, diag);
        table.push_back(std::move(stage));
    }
    return table;
}

SwitchMappingTable ParseSwitchMappingTable(const nlohmann::json& root,
                                           const std::set<std::string>& reservedTopLevelKeys,
                                           const std::string& diagContext,
                                           rs::legacy_pi::Diagnostics& diag) {
    SwitchMappingTable table;
    for (const auto& item : root.items()) {
        const std::string& key = item.key();
        if (key == "version" || reservedTopLevelKeys.contains(key)) continue;
        if (key == "type") {
            if (!item.value().is_object()) Warn(diag, diagContext, "typeがobjectではないため無視しました");
            else table.type = ParseSwitchValueTable(item.value(), diagContext, diag);
        } else if (key == "destination") {
            if (!item.value().is_object()) Warn(diag, diagContext, "destinationがobjectではないため無視しました");
            else table.destination = ParseSwitchValueTable(item.value(), diagContext, diag);
        } else if (key == "lineName") {
            if (!item.value().is_object()) Warn(diag, diagContext, "lineNameがobjectではないため無視しました");
            else table.lineName = ParseSwitchValueTable(item.value(), diagContext, diag);
        } else if (key == "lastDepartureStation") {
            if (!item.value().is_object()) Warn(diag, diagContext, "lastDepartureStationがobjectではないため無視しました");
            else table.lastDepartureStation = ParseSwitchValueTable(item.value(), diagContext, diag);
        } else if (key == "trainNumber") {
            if (!item.value().is_array()) Warn(diag, diagContext, "trainNumberがarrayではないため無視しました");
            else table.trainNumber = ParseDigitSwitchTable(item.value(), diagContext, diag);
        } else if (key == "dutyNumber") {
            if (!item.value().is_array()) Warn(diag, diagContext, "dutyNumberがarrayではないため無視しました");
            else table.dutyNumber = ParseDigitSwitchTable(item.value(), diagContext, diag);
        } else if (key == "stopStations") {
            Warn(diag, diagContext, "stopStationsは対応表として使用できないため無視しました");
        } else {
            Warn(diag, diagContext, "未知のトップレベルキー '" + key + "' を無視しました");
        }
    }
    return table;
}

} // namespace rs::legacy_pi::sidecar
