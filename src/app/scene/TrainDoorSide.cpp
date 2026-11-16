#include "TrainDoorSide.h"

#include <algorithm>
#include <unordered_map>

namespace rs::app {
namespace {

bool IsDoorSideFlippedFallback(const rs::world::TrackPosition& carHead, bool carReversed) {
    return carReversed == (carHead.direction < 0);
}

} // namespace

DoorSideReference ResolveDoorSideReference(const rs::world::TrackGraph& graph,
                                           const rs::world::PlatformStopInfo& platform,
                                           const rs::world::TrackPosition& leadHead) {
    DoorSideReference reference;
    if (platform.railEdges.empty()) return reference;

    const bool leadHeadIsOnPlatform =
        std::find(platform.railEdges.begin(), platform.railEdges.end(), leadHead.edge) !=
        platform.railEdges.end();
    const rs::world::Guid referenceEdgeId =
        leadHeadIsOnPlatform ? leadHead.edge : platform.railEdges.front();
    const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator edgeIt =
        graph.edges.find(referenceEdgeId);
    if (edgeIt == graph.edges.end()) return reference;

    const double referenceS = leadHeadIsOnPlatform
                                  ? leadHead.s
                                  : rs::world::EdgeLength(edgeIt->second) * 0.5;
    const rs::world::TrackFrame frame = rs::world::EvaluateAt(edgeIt->second, referenceS, +1);
    reference.valid = true;
    reference.rightWorld = rs::core::Vec3::Cross(frame.up, frame.tangent);
    return reference;
}

bool IsDoorSideFlipped(const rs::world::TrackGraph& graph,
                       const DoorSideReference& reference,
                       const rs::world::TrackPosition& carHead, bool carReversed) {
    if (!reference.valid) return IsDoorSideFlippedFallback(carHead, carReversed);

    const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator edgeIt =
        graph.edges.find(carHead.edge);
    if (edgeIt == graph.edges.end()) return IsDoorSideFlippedFallback(carHead, carReversed);

    const rs::world::TrackFrame frame =
        rs::world::EvaluateAt(edgeIt->second, carHead.s, carHead.direction);
    rs::core::Vec3 carRight = rs::core::Vec3::Cross(frame.up, frame.tangent);
    if (carReversed) carRight = carRight * -1.0f;

    // 基準edge上では4通りの(dir, reversed)すべてでフォールバック式
    // (carReversed == (direction < 0))と一致する。他edge上の車両へはこの判定で一般化する。
    return rs::core::Vec3::Dot(carRight, reference.rightWorld) > 0.0f;
}

} // namespace rs::app
