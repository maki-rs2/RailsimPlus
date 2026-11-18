#pragma once

#include "EdgeSplitMerge.h"
#include "PlatformStopInfo.h"
#include "SpeedSignal.h"

namespace rs::world {

struct TrackGraphReferenceChange {
    enum class Kind { Split, Merge, Remove };
    Kind kind = Kind::Remove;
    Guid firstEdge = NilGuid();
    Guid secondEdge = NilGuid();
    double splitS = 0.0;
    EdgeMergeRemap merge;
    Guid winnerEdge = NilGuid();
};

// 形や姿勢を巻き戻さず、付け替えた参照だけをUndoで戻すために控える。
struct TrackGraphReferenceSnapshot {
    struct NodeLinks {
        Guid node;
        std::vector<TrackNode::Link> links;
        std::array<int, 2> activeSlot;
    };
    struct EdgeMembers {
        Guid edge;
        std::vector<Guid> members;
    };
    std::vector<NodeLinks> nodes;
    std::vector<EdgeMembers> edges;
    std::vector<PlatformStopInfo> platforms;
    std::vector<Block> blocks;
    std::vector<SpeedSignal> signals;
};

// node/edgeの骨格を編集した後に呼ぶ。結合の所属は長い側、分割の所属は両側へ引き継ぐ。
TrackGraphReferenceSnapshot RemapTrackGraphReferences(TrackGraph& graph,
                                                       const TrackGraphReferenceChange& change);
void RestoreTrackGraphReferences(TrackGraph& graph, const TrackGraphReferenceSnapshot& snapshot);

} // namespace rs::world
