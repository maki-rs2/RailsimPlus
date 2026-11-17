#include "SignalPluginRules.h"
#include "../../plugins/schema/PluginSchema.h"

#include <algorithm>
#include <cmath>

namespace rs::app {

std::vector<int> SignalSpeedChoices(const rs::legacy_pi::sidecar::StationSidecar& sidecar) {
    if (!sidecar.loaded || !sidecar.hasSignal) return {};
    return rs::legacy_pi::sidecar::BuildAspectsFromSignalTable(sidecar.signal);
}

std::string ValidateSignalSidecar(const rs::legacy_pi::sidecar::StationSidecar& sidecar) {
    if (!sidecar.loaded) return "station.jsonを読めませんでした";
    if (!sidecar.hasSignal) return "このプラグインには信号の速度表がありません";
    if (SignalSpeedChoices(sidecar).empty()) return "信号の速度表に使える段がありません";
    return std::string();
}

std::vector<rs::world::SignalAspectStage> SignalStagesFromSidecar(
    const rs::legacy_pi::sidecar::StationSidecar& sidecar) {
    std::vector<rs::world::SignalAspectStage> stages;
    if (!sidecar.loaded || !sidecar.hasSignal) return stages;
    const std::vector<int> speeds = rs::legacy_pi::sidecar::BuildAspectsFromSignalTable(sidecar.signal);
    const std::vector<size_t> sourceIndices =
        rs::legacy_pi::sidecar::BuildAspectSourceIndices(sidecar.signal);
    stages.reserve(speeds.size());
    for (size_t stage = 0; stage < speeds.size(); ++stage) {
        stages.push_back({speeds[stage], static_cast<int>(sourceIndices[stage])});
    }
    return stages;
}

namespace {

bool HasStageAt(const std::vector<rs::world::SignalAspectStage>& stages, int speedKmh) {
    return std::any_of(stages.begin(), stages.end(), [speedKmh](const rs::world::SignalAspectStage& stage) {
        return stage.speedKmh == speedKmh;
    });
}

} // namespace

std::vector<int> SignalAddableSpeeds(const rs::legacy_pi::sidecar::StationSidecar& sidecar,
                                     const std::vector<rs::world::SignalAspectStage>& stages) {
    std::vector<int> speeds = SignalSpeedChoices(sidecar);
    speeds.erase(std::remove_if(speeds.begin(), speeds.end(),
                                [&stages](int speedKmh) { return HasStageAt(stages, speedKmh); }),
                 speeds.end());
    return speeds;
}

std::optional<std::vector<rs::world::SignalAspectStage>> SignalStagesWithAdded(
    const rs::legacy_pi::sidecar::StationSidecar& sidecar,
    const std::vector<rs::world::SignalAspectStage>& stages, int speedKmh) {
    if (HasStageAt(stages, speedKmh)) return std::nullopt;
    for (const rs::world::SignalAspectStage& candidate : SignalStagesFromSidecar(sidecar)) {
        if (candidate.speedKmh != speedKmh) continue;
        std::vector<rs::world::SignalAspectStage> result = stages;
        result.push_back(candidate);
        std::sort(result.begin(), result.end(),
                  [](const rs::world::SignalAspectStage& left, const rs::world::SignalAspectStage& right) {
                      return left.speedKmh < right.speedKmh;
                  });
        return result;
    }
    return std::nullopt;
}

std::optional<std::vector<rs::world::SignalAspectStage>> SignalStagesWithRemoved(
    const std::vector<rs::world::SignalAspectStage>& stages, int speedKmh) {
    if (speedKmh == 0 || !HasStageAt(stages, speedKmh)) return std::nullopt;
    std::vector<rs::world::SignalAspectStage> result = stages;
    result.erase(std::remove_if(result.begin(), result.end(),
                                [speedKmh](const rs::world::SignalAspectStage& stage) {
                                    return stage.speedKmh == speedKmh;
                                }),
                 result.end());
    return result;
}

std::optional<rs::legacy_pi::sidecar::SwitchAssignment> SignalAspectSwitchAssignment(
    const rs::world::SpeedSignal& signal, const rs::legacy_pi::sidecar::StationSidecar& sidecar,
    std::optional<int> displayStage) {
    if (!sidecar.loaded || !sidecar.hasSignal) return std::nullopt;
    const rs::legacy_pi::sidecar::SignalTable& table = sidecar.signal;
    if (displayStage.has_value() && *displayStage >= 0 &&
        static_cast<size_t>(*displayStage) < signal.stages.size()) {
        const int sourceIndex = signal.stages[static_cast<size_t>(*displayStage)].sidecarIndex;
        if (sourceIndex >= 0 && static_cast<size_t>(sourceIndex) < table.size()) {
            return table[static_cast<size_t>(sourceIndex)].assignment;
        }
    }
    const std::optional<size_t> darkIndex = rs::legacy_pi::sidecar::FindDarkStageSourceIndex(table);
    if (!darkIndex.has_value()) return std::nullopt;
    return table[*darkIndex].assignment;
}

std::set<std::string> SignalEngineControlledSwitchNames(
    const rs::legacy_pi::sidecar::StationSidecar& sidecar) {
    std::set<std::string> names;
    if (!sidecar.loaded || !sidecar.hasSignal) return names;
    for (const rs::legacy_pi::sidecar::SignalStage& stage : sidecar.signal) {
        for (const std::pair<const std::string, int>& entry : stage.assignment) names.insert(entry.first);
    }
    return names;
}

std::vector<bool> SignalEngineControlledFlags(const rs::legacy_pi::sidecar::StationSidecar& sidecar,
                                              const std::vector<rs::legacy_pi::SwitchDef>& switchDefs) {
    const std::set<std::string> names = SignalEngineControlledSwitchNames(sidecar);
    std::vector<bool> flags(switchDefs.size(), false);
    // 割り当ての解決(ResolveSwitchAssignmentChanges)は同名なら最初の宣言だけに書き、
    // "_"始まりは書かない。実際に書かれるスイッチだけを制御対象にする。
    std::set<std::string> seen;
    for (std::size_t index = 0; index < switchDefs.size(); ++index) {
        const std::string& name = switchDefs[index].name;
        if (name.empty() || name.front() == '_' || names.count(name) == 0) continue;
        flags[index] = seen.insert(name).second;
    }
    return flags;
}

std::string SignalPluginDisplayName(const std::string& folderUtf8) {
    std::string trimmed = folderUtf8;
    while (!trimmed.empty() && (trimmed.back() == '/' || trimmed.back() == '\\')) trimmed.pop_back();
    const std::size_t slash = trimmed.find_last_of("/\\");
    return slash == std::string::npos ? trimmed : trimmed.substr(slash + 1);
}

} // namespace rs::app
