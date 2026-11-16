#include "PlatformChain.h"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

#include <spdlog/spdlog.h>

namespace rs::world {

namespace {

// scanStartのedgeがgraphに無ければ、番線と違うシーンのgraphで引いている証拠になる。
// 2つのカウントだけでは同一シーンでのチェーン不連続と区別が付かないため併記する。
void WarnUnresolvedPlatformChain(const TrackGraph& graph, const PlatformStopInfo& platform,
                                 const TrackPosition& scanStart, const Guid& entryEdge,
                                 size_t resolvedEdgeCount) {
    spdlog::warn("[platform_chain] 番線チェーンを解決できません: 番線edge数={}, 解決edge数={}, "
                 "scanStart.edge={:x}(graph内={}) entryEdge={:x}(番線内={})",
                 platform.railEdges.size(), resolvedEdgeCount, scanStart.edge[0],
                 graph.edges.find(scanStart.edge) != graph.edges.end(), entryEdge[0],
                 std::find(platform.railEdges.begin(), platform.railEdges.end(), entryEdge) !=
                     platform.railEdges.end());
}

} // namespace

PlatformChain ResolvePlatformChain(const TrackGraph& graph, const PlatformStopInfo& platform,
                                   TrackPosition scanStart, Guid entryEdge, double maxSearchM) {
    PlatformChain chain;
    const std::unordered_set<Guid, GuidHash> platformEdges(platform.railEdges.begin(),
                                                            platform.railEdges.end());
    if (platform.railEdges.empty()) {
        WarnUnresolvedPlatformChain(graph, platform, scanStart, entryEdge, chain.orderedEdges.size());
        return chain;
    }

    const std::vector<EdgeAhead> ahead = ScanEdgesAhead(graph, scanStart, maxSearchM);
    std::vector<EdgeAhead>::const_iterator entryIt = ahead.end();
    for (std::vector<EdgeAhead>::const_iterator it = ahead.begin(); it != ahead.end(); ++it) {
        if (it->edge == entryEdge) {
            entryIt = it;
            break;
        }
    }
    if (entryIt == ahead.end() || platformEdges.find(entryEdge) == platformEdges.end()) {
        WarnUnresolvedPlatformChain(graph, platform, scanStart, entryEdge, chain.orderedEdges.size());
        return chain;
    }

    for (std::vector<EdgeAhead>::const_iterator it = entryIt; it != ahead.end(); ++it) {
        if (platformEdges.find(it->edge) == platformEdges.end()) break;
        chain.orderedEdges.push_back(it->edge);
    }
    chain.entryDistance = entryIt->distance;

    if (platformEdges.find(scanStart.edge) != platformEdges.end()) {
        TrackPosition reverseStart = scanStart;
        reverseStart.direction = -reverseStart.direction;
        const std::vector<EdgeAhead> behind = ScanEdgesAhead(graph, reverseStart, maxSearchM);
        std::vector<Guid> precedingEdges;
        double backwardLength = scanStart.direction >= 0 ? scanStart.s : 0.0;

        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator startEdgeIt =
            graph.edges.find(scanStart.edge);
        if (startEdgeIt != graph.edges.end() && scanStart.direction < 0) {
            backwardLength = EdgeLength(startEdgeIt->second) - scanStart.s;
        }

        for (size_t index = 1; index < behind.size(); ++index) {
            const EdgeAhead& edgeAhead = behind[index];
            if (platformEdges.find(edgeAhead.edge) == platformEdges.end()) break;
            const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
                graph.edges.find(edgeAhead.edge);
            if (edgeIt == graph.edges.end()) break;
            precedingEdges.push_back(edgeAhead.edge);
            backwardLength += EdgeLength(edgeIt->second);
        }

        std::reverse(precedingEdges.begin(), precedingEdges.end());
        chain.orderedEdges.insert(chain.orderedEdges.begin(), precedingEdges.begin(),
                                  precedingEdges.end());
        chain.entryDistance = -backwardLength;
    }

    for (const Guid& edgeGuid : chain.orderedEdges) {
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
            graph.edges.find(edgeGuid);
        if (edgeIt != graph.edges.end()) chain.totalLength += EdgeLength(edgeIt->second);
    }

    if (chain.orderedEdges.size() != platform.railEdges.size()) {
        WarnUnresolvedPlatformChain(graph, platform, scanStart, entryEdge, chain.orderedEdges.size());
        return chain;
    }

    chain.ok = true;
    return chain;
}

double ResolveStopOffset(StopReference kind, double platformLengthM, double trainLengthM) {
    const double totalLengthM = platformLengthM + trainLengthM;
    if (totalLengthM <= 0.0) {
        return 0.0;
    }

    switch (kind) {
    case StopReference::Front:
        return (platformLengthM - trainLengthM) / totalLengthM;
    case StopReference::Center:
        return 0.0;
    case StopReference::Rear:
        return (trainLengthM - platformLengthM) / totalLengthM;
    }
    return 0.0;
}

double PlatformStopTargetDistance(const PlatformChain& chain, double trainLengthM, double offset,
                                  double distM) {
    return chain.entryDistance +
           0.5 * (offset + 1.0) * (chain.totalLength + trainLengthM) + distM;
}

} // namespace rs::world
