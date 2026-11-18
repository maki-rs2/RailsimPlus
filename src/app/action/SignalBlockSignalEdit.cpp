//
// SignalBlockSignalEdit.cpp - SignalBlockAction のうち、既にある信号1本を変える操作
//
#include "SignalBlockAction.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "../../editor/tool/SpeedSignalCommand.h"
#include "../../world/alignment/SpeedSignalRegistry.h"
#include "../../editor/tool/Editor.h"
#include "../../plugins/schema/SwitchDef.h"

namespace rs::app {

void SignalBlockAction::RemoveSpeedSignal(const rs::world::Guid& signalGuid) {
    if (rs::world::FindSpeedSignal(editor_.graph, signalGuid) == nullptr) return;
    if (editor_.ExecutePlatformCommand(std::make_unique<rs::editor::RemoveSpeedSignalCommand>(signalGuid)))
        editor_.NoteRemoved();
}

rs::world::SpeedSignalPlacementError SignalBlockAction::SetSpeedSignalDirection(const rs::world::Guid& signalGuid,
    int direction, const rs::world::WarpLinkTable* warpLinks, std::size_t sceneIndex) {
    const rs::world::SpeedSignal* signal = rs::world::FindSpeedSignal(editor_.graph, signalGuid);
    const int normalized = direction < 0 ? -1 : +1;
    if (signal == nullptr || signal->direction == normalized) return rs::world::SpeedSignalPlacementError::None;
    const rs::world::SpeedSignalPlacementError error = rs::world::ValidateSpeedSignalPlacement(
        editor_.graph, signal->edge, signal->s, normalized, signal->block, signal->guid, warpLinks, sceneIndex,
        signal->kind);
    if (error != rs::world::SpeedSignalPlacementError::None) return error;
    rs::editor::SpeedSignalPropertyDelta delta;
    delta.direction = normalized;
    editor_.ExecutePlatformCommand(
        std::make_unique<rs::editor::SetSpeedSignalPropertyCommand>(signalGuid, std::move(delta)));
    return rs::world::SpeedSignalPlacementError::None;
}

void SignalBlockAction::AddSpeedSignalStage(const rs::world::Guid& signalGuid, int speedKmh) {
    const rs::world::SpeedSignal* signal = rs::world::FindSpeedSignal(editor_.graph, signalGuid);
    if (signal == nullptr || signal->pluginFolderUtf8.empty()) return;
    std::optional<std::vector<rs::world::SignalAspectStage>> stages = SignalStagesWithAdded(
        sidecarSource_.Sidecar(signal->pluginFolderUtf8), signal->stages, speedKmh);
    if (!stages.has_value()) return;
    rs::editor::SpeedSignalPropertyDelta delta;
    delta.stages = std::move(stages);
    editor_.ExecutePlatformCommand(
        std::make_unique<rs::editor::SetSpeedSignalPropertyCommand>(signalGuid, std::move(delta)));
}

void SignalBlockAction::RemoveSpeedSignalStage(const rs::world::Guid& signalGuid, int speedKmh) {
    const rs::world::SpeedSignal* signal = rs::world::FindSpeedSignal(editor_.graph, signalGuid);
    if (signal == nullptr) return;
    std::optional<std::vector<rs::world::SignalAspectStage>> stages =
        SignalStagesWithRemoved(signal->stages, speedKmh);
    if (!stages.has_value()) return;
    rs::editor::SpeedSignalPropertyDelta delta;
    delta.stages = std::move(stages);
    editor_.ExecutePlatformCommand(
        std::make_unique<rs::editor::SetSpeedSignalPropertyCommand>(signalGuid, std::move(delta)));
}

void SignalBlockAction::SetSpeedSignalSwitch(const rs::world::Guid& signalGuid, std::size_t index,
                                             std::int32_t value) {
    const rs::world::SpeedSignal* signal = rs::world::FindSpeedSignal(editor_.graph, signalGuid);
    if (signal == nullptr || signal->pluginFolderUtf8.empty()) return;
    const std::vector<rs::legacy_pi::SwitchDef>* defs = sidecarSource_.SwitchDefs(signal->pluginFolderUtf8);
    if (defs == nullptr || index >= defs->size()) return;
    const std::vector<bool> controlled =
        SignalEngineControlledFlags(sidecarSource_.Sidecar(signal->pluginFolderUtf8), *defs);
    if (controlled[index]) return;
    value = ClampSwitchValue((*defs)[index], value);
    const std::int32_t current = index < signal->switchOptions.size() ? signal->switchOptions[index] : 0;
    if (value == current) return;
    std::vector<std::int32_t> options = signal->switchOptions;
    if (options.size() <= index) options.resize(index + 1, 0);
    options[index] = value;
    rs::editor::SpeedSignalPropertyDelta delta;
    delta.switchOptions = std::move(options);
    editor_.ExecutePlatformCommand(
        std::make_unique<rs::editor::SetSpeedSignalPropertyCommand>(signalGuid, std::move(delta)));
}

std::vector<SwitchRowViewModel> SignalBlockAction::SwitchRowsOfSignal(const rs::world::Guid& signalGuid) const {
    const rs::world::SpeedSignal* signal = rs::world::FindSpeedSignal(editor_.graph, signalGuid);
    if (signal == nullptr || signal->pluginFolderUtf8.empty()) return {};
    const std::vector<rs::legacy_pi::SwitchDef>* defs = sidecarSource_.SwitchDefs(signal->pluginFolderUtf8);
    if (defs == nullptr) return {};
    const std::vector<bool> controlled =
        SignalEngineControlledFlags(sidecarSource_.Sidecar(signal->pluginFolderUtf8), *defs);
    std::vector<SwitchRowViewModel> rows = BuildSwitchRows(
        *defs, [signal](std::size_t index, const rs::legacy_pi::SwitchDef&) -> std::int32_t {
            return index < signal->switchOptions.size() ? signal->switchOptions[index] : 0;
        });
    for (std::size_t index = 0; index < rows.size(); ++index) {
        rows[index].linkedToBlock = controlled[index];
    }
    return rows;
}

void SignalBlockAction::SetSpeedSignalForceRed(const rs::world::Guid& signalGuid, bool forced) {
    aspectSource_.Aspects().SetForceRed(signalGuid, forced);
}

void SignalBlockAction::SetSpeedSignalEntryPermit(const rs::world::Guid& signalGuid, bool permitted) {
    aspectSource_.Aspects().SetEntryPermit(signalGuid, permitted);
}

} // namespace rs::app
