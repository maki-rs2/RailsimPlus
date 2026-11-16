#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "BlockSpeedLimit.h"
#include "TrackForwardScan.h"
#include "WarpLinkTable.h"

namespace rs::world {

struct WarpForwardScanResult {
    bool found = false;
    size_t sceneIndex = 0;
    Guid foundEdge = NilGuid();
    double distance = 0.0;
    TrackPosition entryPos;
    int warpHops = 0;
    bool atEnd = false;
};

struct EdgeAheadAcrossScenes {
    size_t sceneIndex = 0;
    Guid edge = NilGuid();
    double distance = 0.0;
    bool alongEdgeDir = true;
    double entryS = 0.0;
    // 走査開始位置からこのedgeへ到達するまでにまたいだwarpの回数。呼び出し側は
    // これが0でないentryのsceneIndex/edgeを、開始シーンのgraphで引いてはならない。
    int warpHops = 0;
};

struct OccupiedRangeAcrossScenes {
    size_t sceneIndex = 0;
    Guid edge = NilGuid();
    double sBegin = 0.0;
    double sEnd = 0.0;
};

// 走査が打ち切られた理由。行き止まり/未開通ポイント/warp未接続で終端したのか、
// maxSearchMを使い切ったのかを呼び出し側が区別するために返す。
struct WarpScanTermination {
    bool atEnd = false;
    bool exceededMax = false;
    ScanEndReason reason = ScanEndReason::None;
};

struct ForwardScanSpan {
    std::vector<EdgeAheadAcrossScenes> entries;
    double scannedM = 0.0;
    WarpScanTermination termination;
};

ForwardScanSpan ScanForwardSpanAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start, double maxSearchM,
    const NodeSlotOverride* slotOverride = nullptr);

std::vector<EdgeAheadAcrossScenes> ScanEdgesAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start, double maxSearchM,
    WarpScanTermination* outTermination = nullptr, const NodeSlotOverride* slotOverride = nullptr);

std::vector<OccupiedRangeAcrossScenes> ScanOccupiedRangesAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start, double maxSearchM,
    const NodeSlotOverride* slotOverride = nullptr);
std::vector<OccupiedRangeAcrossScenes> ScanOccupiedRangesAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start, double maxSearchM,
    const ForwardScanSpan& span, const NodeSlotOverride* slotOverride = nullptr);

std::vector<SpeedLimitAhead> ScanSpeedLimitsAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start, double maxSearchM,
    const NodeSlotOverride* slotOverride = nullptr,
    const SignalSpeedForTrainFn& signalSpeed = SignalSpeedForTrainFn());
std::vector<SpeedLimitAhead> ScanSpeedLimitsAheadAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start, double maxSearchM,
    const ForwardScanSpan& span, const NodeSlotOverride* slotOverride = nullptr,
    const SignalSpeedForTrainFn& signalSpeed = SignalSpeedForTrainFn());

WarpForwardScanResult FindForwardEdgeAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start,
    const std::function<bool(size_t sceneIndex, const Guid& edge)>& isTarget,
    double maxSearchM, const NodeSlotOverride* slotOverride = nullptr);
WarpForwardScanResult FindForwardEdgeAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable, size_t startScene, TrackPosition start,
    const std::function<bool(size_t sceneIndex, const Guid& edge)>& isTarget,
    double maxSearchM, const ForwardScanSpan& span, const NodeSlotOverride* slotOverride = nullptr);

} // namespace rs::world
