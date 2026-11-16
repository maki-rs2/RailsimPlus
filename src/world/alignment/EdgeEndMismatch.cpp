//
// EdgeEndMismatch.cpp - edge の端と両端ノード姿勢の一致検査
//
#include "EdgeEndMismatch.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rs::world {

using rs::core::Vec3;

namespace {

void CheckEnd(std::vector<EdgeEndMismatch>& out, const Guid& edgeGuid, bool atStart,
              const TrackNodeSample& sample, const TrackNode& node, float sign, double posTolM,
              double dirTolDeg) {
    EdgeEndMismatch m;
    m.edge = edgeGuid;
    m.atStart = atStart;
    m.positionErrorM = (sample.pos - node.pos).Length();
    const Vec3 expected = (node.tangent * sign).Normalized();
    m.directionErrorDeg = rs::core::AngleBetweenDeg(sample.tangent.Normalized(), expected);
    if (m.positionErrorM > posTolM || m.directionErrorDeg > dirTolDeg ||
        !std::isfinite(m.positionErrorM) || !std::isfinite(m.directionErrorDeg))
        out.push_back(m);
}

}  // namespace

std::vector<EdgeEndMismatch> FindEdgeEndMismatches(const TrackGraph& graph, double posTolM,
                                                   double dirTolDeg) {
    std::vector<EdgeEndMismatch> out;
    for (const std::pair<const Guid, TrackEdge>& entry : graph.edges) {
        const TrackEdge& edge = entry.second;
        if (edge.spline.size() < 2) continue;
        const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator itA =
            graph.nodes.find(edge.nodeA);
        const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator itB =
            graph.nodes.find(edge.nodeB);
        if (itA == graph.nodes.end() || itB == graph.nodes.end()) continue;
        CheckEnd(out, edge.guid, true, edge.spline.front(), itA->second, edge.nodeATangentSign,
                 posTolM, dirTolDeg);
        CheckEnd(out, edge.guid, false, edge.spline.back(), itB->second, edge.nodeBTangentSign,
                 posTolM, dirTolDeg);
    }
    return out;
}

}  // namespace rs::world
