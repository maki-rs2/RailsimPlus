#include "SignalModelSet.h"

#include "../../plugins/schema/sidecar/SwitchAssignmentResolve.h"
#include "../action/SignalPluginRules.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {

namespace {

// 末尾の0を落とす(MakeStaticInstanceKeyの正規化と同じ。空=全スイッチ既定0)。
void TrimTrailingZeros(std::vector<int32_t>& options) {
    while (!options.empty() && options.back() == 0) options.pop_back();
}

} // namespace

std::vector<int32_t> SignalModelSet::ComputeSwitchOptions(const rs::world::SpeedSignal& signal,
                                                          std::optional<int> displayStage,
                                                          const SignalModelSources& sources) const {
    std::vector<int32_t> options;
    if (!sources.sidecar || !sources.switchDefs) return options;
    const std::vector<rs::legacy_pi::SwitchDef>* defs = sources.switchDefs(signal.pluginFolderUtf8);
    if (defs == nullptr) return options;
    // ユーザーが選んだ値を土台にする(足りない分は0、余りは捨てる)。
    options = signal.switchOptions;
    options.resize(defs->size(), 0);
    const std::optional<rs::legacy_pi::sidecar::SwitchAssignment> assignment =
        SignalAspectSwitchAssignment(signal, sources.sidecar(signal.pluginFolderUtf8), displayStage);
    if (assignment.has_value()) {
        // 現示の割り当てが書くスイッチだけ、土台の値を上書きする。
        const std::vector<std::pair<size_t, int32_t>> changes =
            rs::legacy_pi::sidecar::ResolveSwitchAssignmentChanges(*defs, options, *assignment);
        for (const std::pair<size_t, int32_t>& change : changes) options[change.first] = change.second;
    }
    TrimTrailingZeros(options);
    return options;
}

void SignalModelSet::Rebuild(const rs::world::TrackGraph& graph, const SignalModelSources& sources) {
    entities_.clear();
    signals_.clear();
    lastStages_.clear();
    idToIndex_.clear();
    pendingChanges_.clear();
    for (const rs::world::SpeedSignal& signal : graph.speedSignals) {
        if (signal.pluginFolderUtf8.empty()) continue;
        const std::optional<int> stage =
            sources.displayStage ? sources.displayStage(signal) : std::nullopt;
        rs::world::SceneEntity entity;
        entity.guid = signal.guid;
        entity.folderUtf8 = signal.pluginFolderUtf8;
        entity.category = rs::world::SceneEntityCategory::Scenery;
        entity.isSignalModel = true;
        rs::world::TrackPlacement placement;
        placement.position = {signal.edge, signal.s, signal.direction};
        placement.yawOffsetDeg = kSignalModelYawOffsetDeg;
        entity.placement = placement;
        entity.switchOptions = ComputeSwitchOptions(signal, stage, sources);
        idToIndex_[entity.guid] = entities_.size();
        entities_.push_back(std::move(entity));
        signals_.push_back(signal);
        lastStages_.push_back(stage);
    }
}

bool SignalModelSet::UpdateAspects(const SignalModelSources& sources) {
    if (!sources.displayStage) return false;
    bool anyChanged = false;
    for (size_t index = 0; index < entities_.size(); ++index) {
        const std::optional<int> stage = sources.displayStage(signals_[index]);
        if (stage == lastStages_[index]) continue;
        lastStages_[index] = stage;
        rs::world::SceneEntity& entity = entities_[index];
        std::vector<int32_t> options = ComputeSwitchOptions(signals_[index], stage, sources);
        if (options == entity.switchOptions) continue;
        bool alreadyPending = false;
        for (const rs::world::SceneRegistry::SwitchChange& change : pendingChanges_) {
            if (change.entityId == entity.guid) alreadyPending = true;
        }
        if (!alreadyPending) pendingChanges_.push_back({entity.guid, entity.switchOptions});
        entity.switchOptions = std::move(options);
        anyChanged = true;
    }
    return anyChanged;
}

std::vector<rs::world::SceneRegistry::SwitchChange> SignalModelSet::TakePendingSwitchChanges() {
    std::vector<rs::world::SceneRegistry::SwitchChange> taken;
    taken.swap(pendingChanges_);
    return taken;
}

const rs::world::SceneEntity* SignalModelSet::FindEntity(const rs::world::Guid& id) const {
    const std::unordered_map<rs::world::Guid, size_t, rs::world::GuidHash>::const_iterator it =
        idToIndex_.find(id);
    return it == idToIndex_.end() ? nullptr : &entities_[it->second];
}

std::optional<rs::world::Guid> SignalModelSet::FindSignalGuid(const rs::world::Guid& id) const {
    const rs::world::SceneEntity* entity = FindEntity(id);
    if (entity == nullptr) return std::nullopt;
    return entity->guid;
}

} // namespace rs::app
