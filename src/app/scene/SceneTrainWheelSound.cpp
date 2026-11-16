#include "SceneTrainWheelSound.h"

#include <optional>
#include <utility>

#include "../../sim/WheelJointTrigger.h"

namespace rs::app {

void CollectCarWheelSounds(SceneSlotPool::Slot& slot, const rs::world::TrackGraph& graph,
                           double odometerM, const WheelSoundFrame& frame,
                           std::vector<rs::sim::WheelSoundEvent>& out) {
    SceneSlotPool::CarRenderSnapshot& car = slot.carSnapshot;
    const bool sameOwner = car.wheelOdometerValid && car.wheelOdometerTrainId == car.carSnapshotTrainId &&
                           car.wheelOdometerGeneration == slot.residency.generation;
    const double before = car.lastWheelOdometerM;
    car.lastWheelOdometerM = odometerM;
    car.wheelOdometerValid = true;
    car.wheelOdometerTrainId = car.carSnapshotTrainId;
    car.wheelOdometerGeneration = slot.residency.generation;
    if (!sameOwner || !frame.audible || before == odometerM) return;

    for (const rs::legacy_plugin::AxleContact& contact : car.carAxleContacts) {
        if (!contact.wheelSound) continue;
        const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator
            edgeIt = graph.edges.find(contact.on.edge);
        if (edgeIt == graph.edges.end()) continue;
        const TrackPluginEntry* rail = FindWheelSoundRail(edgeIt->second, contact.on.s, frame.findRail);
        if (rail == nullptr) continue;

        // 車軸が編成先頭から後ろへ離れている距離。通算走行距離から引くと、車軸が通ってきた距離になる。
        const double axleOffsetM = car.carSnapshotPathOffsetM + contact.chaseOffsetM;
        const std::optional<double> startMs = rs::sim::DetectWheelJointCrossing(
            before - axleOffsetM, odometerM - axleOffsetM, rail->def.railSound.jointInterval,
            frame.frameMs);
        if (!startMs.has_value()) continue;

        rs::sim::WheelSoundEvent event;
        event.railFolder = rail->folderUtf8;
        event.wavPath = ResolveWheelSoundPath(*rail);
        event.worldPos[0] = contact.worldPos.x;
        event.worldPos[1] = contact.worldPos.y;
        event.worldPos[2] = contact.worldPos.z;
        event.startOffsetSeconds = *startMs / 1000.0;
        out.push_back(std::move(event));
    }
}

} // namespace rs::app
