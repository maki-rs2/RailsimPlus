#include "AutoCant.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "../../core/Epsilon.h"
#include "../geometry/EditorGeometryTuning.h"
#include "TrackGraph.h"
#include "TrackNode.h"

namespace rs::world {

namespace {

//水平の曲がりの向き。+1=進行方向の右(cross(上, 接線)の側)へ曲がる、-1=左、0=曲がらない。
//縦円弧(axisTiltRad が大きい)は水平の曲がりに寄与しないので 0 とする。
float TurnSign(const AlignmentSegment& seg) {
    float radius = 0.0f;
    if (seg.kind == AlignmentSegment::Kind::Arc) {
        radius = seg.radius;
        if (std::fabs(std::cos(seg.axisTiltRad)) < 0.1f) return 0.0f;
        if (std::cos(seg.axisTiltRad) < 0.0f) radius = -radius;
    } else if (seg.kind == AlignmentSegment::Kind::Transition) {
        radius = std::fabs(seg.radius) > rs::core::kZeroLengthEps ? seg.radius : seg.radiusEnd;
    }
    if (std::fabs(radius) < rs::core::kZeroLengthEps) return 0.0f;
    return radius > 0.0f ? 1.0f : -1.0f;
}

float TangentSignAtEnd(const TrackEdge& edge, int end) {
    return end == 0 ? edge.nodeATangentSign : edge.nodeBTangentSign;
}

}  // namespace

float AutoCantPlateau(const std::vector<AlignmentSegment>& curve, float cantAutoMax) {
    for (const AlignmentSegment& seg : curve) {
        const float turn = TurnSign(seg);
        //ComposeCantUp は正のカントで up を右側へ倒す(右が低い)。右へ曲がる曲線は
        //内側=右を高くしたいので負、左へ曲がる曲線は正にする。
        if (turn != 0.0f) return -turn * std::fabs(cantAutoMax);
    }
    return 0.0f;
}

float AutoCantJoint(float ownPlateau, float neighborPlateau) {
    if (ownPlateau == 0.0f || neighborPlateau == 0.0f) return 0.0f;
    if ((ownPlateau > 0.0f) != (neighborPlateau > 0.0f)) return 0.0f;
    const float magnitude = std::min(std::fabs(ownPlateau), std::fabs(neighborPlateau));
    return ownPlateau > 0.0f ? magnitude : -magnitude;
}

float NeighborAutoPlateau(const TrackGraph& graph, const TrackEdge& edge, int end) {
    const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator nodeIt =
        graph.nodes.find(end == 0 ? edge.nodeA : edge.nodeB);
    if (nodeIt == graph.nodes.end() || nodeIt->second.links.size() != 2) return 0.0f;
    for (const TrackNode::Link& link : nodeIt->second.links) {
        if (link.edge == edge.guid) continue;
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator nbIt =
            graph.edges.find(link.edge);
        if (nbIt == graph.edges.end() || !nbIt->second.cantAuto) return 0.0f;
        const TrackEdge& neighbor = nbIt->second;
        const float plateau = AutoCantPlateau(neighbor.curve, neighbor.cantAutoMax);
        //進行方向が逆向きにつながる相手は右の向きも逆なので、符号を自分の向きへ合わせる。
        const bool sameDirection =
            TangentSignAtEnd(edge, end) * TangentSignAtEnd(neighbor, link.end) >= 0.0f;
        return sameDirection ? plateau : -plateau;
    }
    return 0.0f;
}

AutoCantValues ResolveAutoCant(const std::vector<AlignmentSegment>& curve, float cantAutoMax,
                               float neighborPlateauA, float neighborPlateauB) {
    AutoCantValues values;
    values.cantMid = AutoCantPlateau(curve, cantAutoMax);
    values.cantA = AutoCantJoint(values.cantMid, neighborPlateauA);
    values.cantB = AutoCantJoint(values.cantMid, neighborPlateauB);
    values.cantTransitionRatio = EditorGeometryTuning::Embedded().autoCantTransitionRatioMeters;
    return values;
}

AutoCantValues EffectiveEdgeCant(const TrackGraph& graph, const TrackEdge& edge) {
    if (!edge.cantAuto)
        return {edge.cantA, edge.cantB, edge.cantMid, edge.cantTransitionRatio};
    return ResolveAutoCant(edge.curve, edge.cantAutoMax, NeighborAutoPlateau(graph, edge, 0),
                           NeighborAutoPlateau(graph, edge, 1));
}

namespace {

//自動モードの edge をまとめて作り直す(重複は除く)。
void RebuildAutoTargets(TrackGraph& graph, const std::vector<Guid>& edges,
                        const std::vector<Guid>& nodes, const AlignmentSampleOptions& opt) {
    std::vector<Guid> targets;
    const auto addIfAuto = [&](const Guid& guid) {
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator it =
            graph.edges.find(guid);
        if (it == graph.edges.end() || !it->second.cantAuto) return;
        if (std::find(targets.begin(), targets.end(), guid) == targets.end())
            targets.push_back(guid);
    };
    for (const Guid& guid : edges) addIfAuto(guid);
    for (const Guid& nodeGuid : nodes) {
        const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator nodeIt =
            graph.nodes.find(nodeGuid);
        if (nodeIt == graph.nodes.end()) continue;
        for (const TrackNode::Link& link : nodeIt->second.links) addIfAuto(link.edge);
    }
    for (const Guid& guid : targets) RebuildEdgeSpline(graph, graph.edges.at(guid), opt);
}

}  // namespace

void RebuildAutoCantAround(TrackGraph& graph, const std::vector<Guid>& edges,
                           const AlignmentSampleOptions& opt) {
    std::vector<Guid> nodes;
    for (const Guid& guid : edges) {
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator it =
            graph.edges.find(guid);
        if (it == graph.edges.end()) continue;
        nodes.push_back(it->second.nodeA);
        nodes.push_back(it->second.nodeB);
    }
    RebuildAutoTargets(graph, edges, nodes, opt);
}

void RebuildAutoCantAtNodes(TrackGraph& graph, const std::vector<Guid>& nodes,
                            const AlignmentSampleOptions& opt) {
    RebuildAutoTargets(graph, {}, nodes, opt);
}

}  // namespace rs::world
