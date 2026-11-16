#include "TrainRuntime.h"

#include <algorithm>

#include "../world/scene/SceneIndexRemap.h"

namespace rs::sim {

std::vector<TrainRuntimeSnapshot> TrainRuntime::Snapshot() const {
    std::vector<TrainRuntimeSnapshot> snapshots;
    snapshots.reserve(runningGroups_.size());
    for (const std::pair<const TrainId, RunningTrain>& entry : runningGroups_) {
        const TrainId trainId = entry.first;
        const RunningTrain& running = entry.second;
        snapshots.push_back({ trainId, running.sceneIndex, running.kin, running.lastTravelReversed,
                              running.stopTargetReversed, running.targetPlatform,
                              running.departedPlatform });
    }
    return snapshots;
}

std::vector<TrainRuntimeSnapshot> TrainRuntime::SnapshotOf(const std::vector<TrainId>& trainIds) const {
    std::vector<TrainRuntimeSnapshot> snapshots;
    for (const TrainRuntimeSnapshot& snapshot : Snapshot()) {
        if (std::find(trainIds.begin(), trainIds.end(), snapshot.trainId) != trainIds.end()) {
            snapshots.push_back(snapshot);
        }
    }
    return snapshots;
}

void TrainRuntime::Restore(const std::vector<TrainRuntimeSnapshot>& snapshots) {
    runningGroups_.clear();
    AddSnapshots(snapshots);
}

void TrainRuntime::AddSnapshots(const std::vector<TrainRuntimeSnapshot>& snapshots) {
    for (const TrainRuntimeSnapshot& snapshot : snapshots) {
        if (rs::world::GuidIsNil(snapshot.trainId)) continue;
        RunningTrain running;
        running.sceneIndex = snapshot.sceneIndex;
        running.kin = snapshot.kin;
        running.lastTravelReversed = snapshot.lastTravelReversed;
        running.stopTargetReversed = snapshot.stopTargetReversed;
        running.targetPlatform = snapshot.targetPlatform;
        running.departedPlatform = snapshot.departedPlatform;
        runningGroups_.emplace(snapshot.trainId, std::move(running));
    }
}

void TrainRuntime::RemapSceneIndices(const std::vector<size_t>& oldToNew) {
    for (std::map<TrainId, RunningTrain>::iterator it = runningGroups_.begin(); it != runningGroups_.end();) {
        RunningTrain& running = it->second;
        running.sceneIndex = rs::world::RemapSceneIndex(oldToNew, running.sceneIndex);
        if (rs::world::IsRemovedScene(running.sceneIndex)) {
            // 消えるSceneにいる編成は走行状態ごと捨てる(呼び出し側が先に編成を消していれば、ここには来ない)。
            pointRoutineCursor_.RemoveTrain(it->first);
            it = runningGroups_.erase(it);
            continue;
        }
        std::vector<OccupiedPathEntry>& entries = running.occupiedPath.entries;
        // 占有edge列は先頭側から並ぶので、消えるSceneに入った所から後ろを切る(列の途中を抜くと繋がりが壊れる)。
        const std::vector<OccupiedPathEntry>::iterator cut =
            std::find_if(entries.begin(), entries.end(), [&oldToNew](const OccupiedPathEntry& entry) {
                return rs::world::IsRemovedScene(rs::world::RemapSceneIndex(oldToNew, entry.sceneIndex));
            });
        entries.erase(cut, entries.end());
        for (OccupiedPathEntry& entry : entries) entry.sceneIndex = rs::world::RemapSceneIndex(oldToNew, entry.sceneIndex);
        ++it;
    }
}

void TrainRuntime::Clear() {
    runningGroups_.clear();
    pointRoutineCursor_.Clear();
}

}  // namespace rs::sim
