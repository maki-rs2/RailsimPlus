//
// StationSidecar.cpp - 駅向けプラグイン拡張JSONの読込
//
#include "StationSidecar.h"

#include <algorithm>
#include <set>

#include "PluginSidecarParsing.h"
#include "../../../platform/text/Encoding.h"

namespace rs::legacy_pi::sidecar {

StationSidecar LoadStationSidecar(const std::wstring& pluginFolder) {
    StationSidecar sidecar;
    rs::legacy_pi::Diagnostics diag;
    const std::string diagContext = rs::platform_text::WideToUtf8(pluginFolder);
    const std::optional<nlohmann::json> root = ReadSidecarJsonFile(
        pluginFolder, kStationSidecarFileName, diagContext, diag);
    if (!root.has_value()) {
        sidecar.diagnostics = diag.Items();
        return sidecar;
    }

    const std::set<std::string> kReservedKeys{"signal"};
    sidecar.table = ParseSwitchMappingTable(*root, kReservedKeys, diagContext, diag);
    if (root->contains("signal")) {
        sidecar.hasSignal = true;
        sidecar.signal = ParseSignalTable((*root)["signal"], diagContext, diag);
    }
    sidecar.loaded = true;
    sidecar.diagnostics = diag.Items();
    return sidecar;
}

std::vector<size_t> BuildAspectSourceIndices(const SignalTable& signalTable) {
    std::vector<size_t> sourceIndices;
    if (signalTable.empty()) return sourceIndices;

    size_t stage0Index = 0;
    for (size_t index = 0; index < signalTable.size(); ++index) {
        if (signalTable[index].speedKmh == 0.0) {
            stage0Index = index;
            break;
        }
    }
    sourceIndices.push_back(stage0Index);

    std::vector<std::pair<double, size_t>> remaining;
    for (size_t index = 0; index < signalTable.size(); ++index) {
        if (index == stage0Index) continue;
        const double speed = signalTable[index].speedKmh;
        if (speed < 0.0) continue;
        const bool alreadySeen =
            std::find_if(remaining.begin(), remaining.end(),
                         [speed](const std::pair<double, size_t>& entry) { return entry.first == speed; }) !=
            remaining.end();
        if (alreadySeen) continue;
        remaining.emplace_back(speed, index);
    }
    std::stable_sort(remaining.begin(), remaining.end(),
                     [](const std::pair<double, size_t>& lhs, const std::pair<double, size_t>& rhs) {
                         return lhs.first < rhs.first;
                     });
    for (const std::pair<double, size_t>& entry : remaining) sourceIndices.push_back(entry.second);
    return sourceIndices;
}

std::optional<size_t> FindDarkStageSourceIndex(const SignalTable& signalTable) {
    for (size_t index = 0; index < signalTable.size(); ++index) {
        if (signalTable[index].speedKmh < 0.0) return index;
    }
    return std::nullopt;
}

std::vector<int> BuildAspectsFromSignalTable(const SignalTable& signalTable) {
    const std::vector<size_t> sourceIndices = BuildAspectSourceIndices(signalTable);
    std::vector<int> aspects;
    if (sourceIndices.empty()) return aspects;
    aspects.push_back(0);
    for (size_t stage = 1; stage < sourceIndices.size(); ++stage) {
        aspects.push_back(static_cast<int>(signalTable[sourceIndices[stage]].speedKmh));
    }
    return aspects;
}

std::vector<SwitchAssignment> BuildAspectAssignmentsFromSignalTable(const SignalTable& signalTable) {
    const std::vector<size_t> sourceIndices = BuildAspectSourceIndices(signalTable);
    std::vector<SwitchAssignment> assignments;
    assignments.reserve(sourceIndices.size());
    for (size_t index : sourceIndices) assignments.push_back(signalTable[index].assignment);
    return assignments;
}

namespace {

void MergeAssignment(SwitchAssignment& result, const SwitchAssignment& assignment) {
    for (const std::pair<const std::string, int>& entry : assignment) {
        result.insert_or_assign(entry.first, entry.second);
    }
}

void MergeValueAssignment(SwitchAssignment& result, const std::optional<SwitchValueTable>& table,
                          const std::string& value) {
    if (!table.has_value()) return;
    const std::map<std::string, SwitchAssignment>::const_iterator entry = table->entries.find(value);
    if (entry != table->entries.end()) MergeAssignment(result, entry->second);
}

void MergeDigitAssignment(SwitchAssignment& result, const std::optional<DigitSwitchTable>& table,
                          const std::string& value) {
    if (!table.has_value()) return;
    const DigitSwitchTable& digits = *table;
    const size_t valueOffset = value.size() > digits.size() ? value.size() - digits.size() : 0;
    const size_t blankCount = digits.size() > value.size() ? digits.size() - value.size() : 0;
    for (size_t index = 0; index < digits.size(); ++index) {
        const std::string character = index < blankCount
            ? " "
            : std::string(1, value[index - blankCount + valueOffset]);
        const std::map<std::string, int>::const_iterator entry =
            digits[index].charToCase.find(character);
        if (entry != digits[index].charToCase.end()) {
            result.insert_or_assign(digits[index].switchLabel, entry->second);
        }
    }
}

} // namespace

SwitchAssignment BuildRunInfoSwitchAssignment(
    const SwitchMappingTable& table, const std::string& type, const std::string& destination,
    const std::string& lineName, const std::string& trainNumber, const std::string& dutyNumber) {
    SwitchAssignment result;
    MergeValueAssignment(result, table.type, type);
    MergeValueAssignment(result, table.destination, destination);
    MergeValueAssignment(result, table.lineName, lineName);
    MergeDigitAssignment(result, table.trainNumber, trainNumber);
    MergeDigitAssignment(result, table.dutyNumber, dutyNumber);
    return result;
}

} // namespace rs::legacy_pi::sidecar
