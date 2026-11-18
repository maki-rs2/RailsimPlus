//
// TrackForwardScan.h - Forward track scanning
//
#pragma once

#include <unordered_set>
#include <vector>

#include "TrackPosition.h"

namespace rs::world {

struct TrackGraph;

// 前方走査が終端した理由。exceededMax で終わった場合は None。
enum class ScanEndReason { None, DeadEnd, SceneBoundary };

struct ForwardScanResult {
    bool found = false;
    Guid foundEdge = NilGuid();
    double distance = 0.0;
    bool atEnd = false;
    bool exceededMax = false;
};

// Returned by ScanSpeedLimitsAheadAcrossScenes.
struct SpeedLimitAhead {
    double distance = 0.0;
    float limitKmh = -1.0f;
};

struct EdgeAhead {
    Guid edge = NilGuid();
    double distance = 0.0;
    bool alongEdgeDir = true;
};

struct ScanTermination {
    bool atEnd = false;
    bool exceededMax = false;
};

ForwardScanResult FindForwardEdge(const TrackGraph& graph, TrackPosition start,
                                   const std::unordered_set<Guid, GuidHash>& targetEdges,
                                   double maxSearchM, const NodeSlotOverride* slotOverride = nullptr);

std::vector<EdgeAhead> ScanEdgesAhead(const TrackGraph& graph, TrackPosition start,
                                       double maxSearchM, ScanTermination* outTermination = nullptr,
                                       const NodeSlotOverride* slotOverride = nullptr);

struct OccupiedRange {
    Guid edge = NilGuid();
    double sBegin = 0.0;
    double sEnd = 0.0;
};

std::vector<OccupiedRange> ScanOccupiedRangesAhead(const TrackGraph& graph, TrackPosition start,
                                                     double maxSearchM,
                                                     const NodeSlotOverride* slotOverride = nullptr);

bool OccupiedRangesOverlap(const std::vector<OccupiedRange>& a,
                           const std::vector<OccupiedRange>& b);

} // namespace rs::world
