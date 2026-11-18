#include "TrackGraphReferences.h"

#include <algorithm>

#include "BlockRegistry.h"
#include "PlatformRegistry.h"
#include "SpeedSignalRegistry.h"
#include "TrackGraph.h"

namespace rs::world {
namespace {

using Kind = TrackGraphReferenceChange::Kind;

void RemapMembership(std::vector<Guid>& edges, const TrackGraphReferenceChange& change,
                     bool ordered) {
    if (change.kind == Kind::Split) {
        const auto found = std::find(edges.begin(), edges.end(), change.firstEdge);
        if (found != edges.end()) {
            if (ordered) edges.insert(found + 1, change.secondEdge);
            else edges.push_back(change.secondEdge);
        }
    } else if (change.kind == Kind::Merge) {
        const bool ownsWinner = std::ranges::find(edges, change.winnerEdge) != edges.end();
        std::erase(edges, change.firstEdge);
        std::erase(edges, change.secondEdge);
        if (ownsWinner) edges.push_back(change.firstEdge);
        if (ordered) {
            std::sort(edges.begin(), edges.end());
            edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        }
    } else {
        std::erase(edges, change.firstEdge);
    }
}

void RemapNode(TrackNode& node, const TrackGraphReferenceChange& change) {
    for (TrackNode::Link& link : node.links) {
        if (change.kind == Kind::Split && link.edge == change.firstEdge && link.end == 1) {
            link.edge = change.secondEdge;
        } else if (change.kind == Kind::Merge) {
            if (link.edge == change.firstEdge) link.end = 0;
            else if (link.edge == change.secondEdge) {
                link.edge = change.firstEdge;
                link.end = 1;
            }
        }
    }
    if (change.kind != Kind::Remove) return;
    std::erase_if(node.links, [&change](const TrackNode::Link& link) {
        return link.edge == change.firstEdge;
    });
    for (const int side : {0, 1}) {
        int& slot = node.activeSlot[static_cast<size_t>(side)];
        slot = std::clamp(slot, 0, std::max(0, node.LinkCountOnSide(side) - 1));
    }
}

void RebuildIndices(TrackGraph& graph) {
    RebuildPlatformIndices(graph);
    RebuildBlockIndices(graph);
    RebuildSpeedSignalIndices(graph);
}

} // namespace

TrackGraphReferenceSnapshot RemapTrackGraphReferences(TrackGraph& graph,
                                                       const TrackGraphReferenceChange& change) {
    TrackGraphReferenceSnapshot snapshot;
    snapshot.platforms = graph.platforms;
    snapshot.blocks = graph.blocks;
    snapshot.signals = graph.speedSignals;
    for (auto& [guid, node] : graph.nodes) {
        if (!std::ranges::any_of(node.links, [&change](const TrackNode::Link& link) {
                return link.edge == change.firstEdge ||
                       (change.kind == Kind::Merge && link.edge == change.secondEdge);
            })) continue;
        snapshot.nodes.push_back({guid, node.links, node.activeSlot});
        RemapNode(node, change);
    }
    // 中心線の同期分割・結合はグループ全体でメンバを確定する。単体削除だけはここで外す。
    if (change.kind == Kind::Remove) {
        for (auto& [guid, edge] : graph.edges) {
            if (std::ranges::find(edge.trackEdges, change.firstEdge) == edge.trackEdges.end()) continue;
            snapshot.edges.push_back({guid, edge.trackEdges});
            std::erase(edge.trackEdges, change.firstEdge);
        }
    }
    for (PlatformStopInfo& platform : graph.platforms) RemapMembership(platform.railEdges, change, true);
    for (Block& block : graph.blocks) RemapMembership(block.edges, change, false);
    if (change.kind != Kind::Split) {
        std::erase_if(graph.blocks, [](const Block& block) { return block.edges.empty(); });
    }
    if (change.kind == Kind::Merge) {
        std::erase_if(graph.platforms, [](const PlatformStopInfo& p) { return p.railEdges.empty(); });
    }
    std::erase_if(graph.speedSignals, [&change](SpeedSignal& signal) {
        if (change.kind == Kind::Remove) return signal.edge == change.firstEdge;
        if (change.kind == Kind::Split && signal.edge == change.firstEdge && signal.s >= change.splitS) {
            signal.edge = change.secondEdge;
            signal.s -= change.splitS;
        } else if (change.kind == Kind::Merge) {
            bool reversed = false;
            if (RemapMergedAnchor(change.merge, signal.edge, signal.s, &reversed) && reversed)
                signal.direction = -signal.direction;
        }
        return false;
    });
    if (change.kind != Kind::Split) PruneSignalsWithoutOwner(graph);
    RebuildIndices(graph);
    return snapshot;
}

void RestoreTrackGraphReferences(TrackGraph& graph, const TrackGraphReferenceSnapshot& snapshot) {
    for (const TrackGraphReferenceSnapshot::NodeLinks& saved : snapshot.nodes) {
        const auto it = graph.nodes.find(saved.node);
        if (it == graph.nodes.end()) continue;
        it->second.links = saved.links;
        it->second.activeSlot = saved.activeSlot;
    }
    for (const TrackGraphReferenceSnapshot::EdgeMembers& saved : snapshot.edges) {
        const auto it = graph.edges.find(saved.edge);
        if (it != graph.edges.end()) it->second.trackEdges = saved.members;
    }
    graph.platforms = snapshot.platforms;
    graph.blocks = snapshot.blocks;
    graph.speedSignals = snapshot.signals;
    RebuildIndices(graph);
}

} // namespace rs::world
