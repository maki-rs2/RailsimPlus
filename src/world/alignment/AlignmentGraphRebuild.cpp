//
// AlignmentGraphRebuild.cpp - TrackGraph の spline 再構築オーケストレーション
//
// SampleAlignment（純粋な曲線サンプリング）とは別レイヤー: TrackGraph を辿って
// edge.spline を再計算するグラフ操作。宣言は Alignment.h に統一する。
//
#include "Alignment.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "../../core/Epsilon.h"
#include "AutoCant.h"
#include "EdgeShapeBuild.h"
#include "../geometry/EditorGeometryTuning.h"

namespace rs::world {

namespace {

// 作った形をedgeへ書き、区間分割を長さ比で再スケールする。
void ApplyBuiltShape(TrackGraph& graph, TrackEdge& edge, EdgeShapeBuild&& built) {
    const double oldLen = edge.spline.empty() ? 0.0 : static_cast<double>(edge.spline.back().s);
    edge.curve = std::move(built.curve);
    edge.spline = std::move(built.spline);
    const double newLen = static_cast<double>(edge.spline.back().s);
    RescaleEdgeSections(edge, oldLen, newLen);
    if (newLen < oldLen) {
        for (SpeedSignal& signal : graph.speedSignals) {
            if (signal.edge == edge.guid) signal.s = std::clamp(signal.s, 0.0, newLen);
        }
    }
}


}  // namespace

bool RebuildEdgeSpline(TrackGraph& graph, TrackEdge& edge,
                       const AlignmentSampleOptions& opt) {
    const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator itA =
        graph.nodes.find(edge.nodeA);
    const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator itB =
        graph.nodes.find(edge.nodeB);
    if (itA == graph.nodes.end() || itB == graph.nodes.end()) return false;

    EdgeShapeBuild built = BuildEdgeShape(MakeEdgeShapeInput(edge, itA->second, itB->second, opt, &graph));
    if (!built.ok) return false;  // 失敗時は edge を一切変えない
    ApplyBuiltShape(graph, edge, std::move(built));
    return true;
}

EdgeRebuildMethod RebuildEdgeSplineWithFallback(TrackGraph& graph, TrackEdge& edge,
                                                const AlignmentSampleOptions& opt) {
    if (RebuildEdgeSpline(graph, edge, opt)) return EdgeRebuildMethod::Exact;
    const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator itA =
        graph.nodes.find(edge.nodeA);
    const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator itB =
        graph.nodes.find(edge.nodeB);
    if (itA == graph.nodes.end() || itB == graph.nodes.end()) return EdgeRebuildMethod::Failed;

    EdgeShapeInput relaxed = MakeEdgeShapeInput(edge, itA->second, itB->second, opt, &graph);
    relaxed.options.endPositionToleranceM = EditorGeometryTuning::Embedded().relaxedEndPositionToleranceM;
    relaxed.options.transitionEndPositionToleranceM = EditorGeometryTuning::Embedded().relaxedEndPositionToleranceM;
    relaxed.options.endDirectionToleranceDeg = EditorGeometryTuning::Embedded().relaxedEndDirectionToleranceDeg;
    EdgeShapeBuild built = BuildEdgeShape(relaxed);
    if (built.ok) {
        ApplyBuiltShape(graph, edge, std::move(built));
        return EdgeRebuildMethod::Relaxed;
    }

    // 両端の向きを弦の向きにそろえた直線。ノードの位置と向きは変えない。
    const rs::core::Vec3 chord = itB->second.pos - itA->second.pos;
    if (chord.Length() < rs::core::kZeroLengthEps) return EdgeRebuildMethod::Failed;
    EdgeShapeInput straight = MakeEdgeShapeInput(edge, itA->second, itB->second, opt, &graph);
    straight.start.dir = chord.Normalized();
    straight.end.dir = straight.start.dir;
    straight.transitionLength = 0.0f;
    built = BuildEdgeShape(straight);
    if (!built.ok) return EdgeRebuildMethod::Failed;
    ApplyBuiltShape(graph, edge, std::move(built));
    DeriveEdgeTangentSigns(graph, edge);
    return EdgeRebuildMethod::Straight;
}

float EdgeTangentSignAgainst(const rs::core::Vec3& nodeTangent, const rs::core::Vec3& edgeDir) {
    return rs::core::Vec3::Dot(nodeTangent, edgeDir) >= 0.0f ? 1.0f : -1.0f;
}

EdgeEndConnection ResolveEdgeEndConnection(const TrackNode& node, const Guid& edge, int end,
                                            const rs::core::Vec3& edgeTangent) {
    for (const TrackNode::Link& link : node.links) {
        if (link.edge != edge || link.end != end) continue;
        const bool positive = (end == 0) == (link.side == 1);
        return {link.side, positive ? 1.0f : -1.0f};
    }
    const float sign = EdgeTangentSignAgainst(node.tangent, edgeTangent);
    const rs::core::Vec3 outgoing = end == 0 ? edgeTangent : edgeTangent * -1.0f;
    const int side = EdgeTangentSignAgainst(node.tangent, outgoing) > 0.0f ? 1 : 0;
    return {side, sign};
}

void DeriveEdgeNodeBTangentSign(const TrackGraph& graph, TrackEdge& edge) {
    if (edge.spline.empty()) return;
    const auto it = graph.nodes.find(edge.nodeB);
    if (it == graph.nodes.end()) return;
    edge.nodeBTangentSign = EdgeTangentSignAgainst(it->second.tangent, edge.spline.back().tangent);
}

void DeriveEdgeTangentSigns(const TrackGraph& graph, TrackEdge& edge) {
    if (edge.spline.empty()) return;
    const auto it = graph.nodes.find(edge.nodeA);
    if (it != graph.nodes.end())
        edge.nodeATangentSign = EdgeTangentSignAgainst(it->second.tangent, edge.spline.front().tangent);
    DeriveEdgeNodeBTangentSign(graph, edge);
}

void RescaleEdgeSections(TrackEdge& edge, double oldLen, double newLen) {
    if (oldLen <= 0.0 || newLen <= 0.0) return;
    // 長さが実質変わっていないなら触らない。カント変更等で長さそのままの再構築が
    // 繰り返し走るため、比率1.0近傍のスケールを毎回かけると境界がじわじわ流される。
    if (std::abs(newLen - oldLen) < kMinSpanLen) return;
    edge.pluginSections.Rescale(oldLen, newLen);
}

std::vector<Guid> CollectEdgesForNode(const TrackGraph& graph, const Guid& nodeGuid) {
    std::vector<Guid> edges;
    const auto nit = graph.nodes.find(nodeGuid);
    if (nit == graph.nodes.end()) return edges;
    for (const TrackNode::Link& link : nit->second.links) {
        if (std::find(edges.begin(), edges.end(), link.edge) == edges.end())
            edges.push_back(link.edge);
    }
    return edges;
}

std::vector<EdgeSectionsSnapshot> CaptureEdgeSections(const TrackGraph& graph,
                                                      const std::vector<Guid>& edges) {
    std::vector<EdgeSectionsSnapshot> snapshots;
    snapshots.reserve(edges.size());
    for (const Guid& e : edges) {
        const auto it = graph.edges.find(e);
        if (it == graph.edges.end()) continue;
        EdgeSectionsSnapshot snap;
        snap.edge = e;
        snap.pluginSections = it->second.pluginSections;
        for (const SpeedSignal& signal : graph.speedSignals) {
            if (signal.edge == e) snap.signalPositions.emplace_back(signal.guid, signal.s);
        }
        snapshots.push_back(std::move(snap));
    }
    return snapshots;
}

void RestoreEdgeSections(TrackGraph& graph, const std::vector<EdgeSectionsSnapshot>& snapshots) {
    for (const EdgeSectionsSnapshot& snap : snapshots) {
        const auto it = graph.edges.find(snap.edge);
        if (it == graph.edges.end()) continue;
        it->second.pluginSections = snap.pluginSections;
        for (const std::pair<Guid, double>& position : snap.signalPositions) {
            for (SpeedSignal& signal : graph.speedSignals) {
                if (signal.guid == position.first) signal.s = position.second;
            }
        }
    }
}

std::vector<EdgeTangentSignsSnapshot> CaptureEdgeTangentSigns(const TrackGraph& graph,
                                                              const std::vector<Guid>& edges) {
    std::vector<EdgeTangentSignsSnapshot> snapshots;
    for (const Guid& e : edges) {
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator it = graph.edges.find(e);
        if (it == graph.edges.end()) continue;
        snapshots.push_back({e, it->second.nodeATangentSign, it->second.nodeBTangentSign});
    }
    return snapshots;
}

void RestoreEdgeTangentSigns(TrackGraph& graph,
                             const std::vector<EdgeTangentSignsSnapshot>& snapshots) {
    for (const EdgeTangentSignsSnapshot& snap : snapshots) {
        const std::unordered_map<Guid, TrackEdge, GuidHash>::iterator it = graph.edges.find(snap.edge);
        if (it == graph.edges.end()) continue;
        it->second.nodeATangentSign = snap.nodeASign;
        it->second.nodeBTangentSign = snap.nodeBSign;
    }
}

bool TangentChangeReversesEdgeDirections(const rs::core::Vec3& oldTangent,
                                         const rs::core::Vec3& newTangent) {
    return rs::core::Vec3::Dot(oldTangent, newTangent) < 0.0f;
}

void FlipEdgeEndSignsAtNode(TrackEdge& edge, const Guid& nodeGuid) {
    if (edge.nodeA == nodeGuid) edge.nodeATangentSign *= -1.0f;
    if (edge.nodeB == nodeGuid) edge.nodeBTangentSign *= -1.0f;
}

void KeepEdgeSignsForTangentChange(TrackGraph& graph, const Guid& nodeGuid,
                                        const rs::core::Vec3& oldTangent,
                                        const rs::core::Vec3& newTangent) {
    if (!TangentChangeReversesEdgeDirections(oldTangent, newTangent)) return;
    for (const Guid& e : CollectEdgesForNode(graph, nodeGuid)) {
        const std::unordered_map<Guid, TrackEdge, GuidHash>::iterator it = graph.edges.find(e);
        if (it != graph.edges.end()) FlipEdgeEndSignsAtNode(it->second, nodeGuid);
    }
}

bool RebuildEdgesForNode(TrackGraph& graph, const Guid& nodeGuid,
                         const AlignmentSampleOptions& opt) {
    // 列挙のコピーを取ってから回す（rebuild 中に map が変化することはないが安全側）。
    const std::vector<Guid> edges = CollectEdgesForNode(graph, nodeGuid);
    bool allOk = true;
    for (const Guid& e : edges) {
        std::unordered_map<Guid, TrackEdge, GuidHash>::iterator eit = graph.edges.find(e);
        if (eit != graph.edges.end() && !RebuildEdgeSpline(graph, eit->second, opt)) allOk = false;
    }
    //自動カントの edge は、隣の curve が出そろってからつなぎ目の値を決め直す。
    RebuildAutoCantAround(graph, edges, opt);
    return allOk;
}

EdgeShapesUnderNodePoses BuildEdgeShapesUnderNodePoses(
    const TrackGraph& graph, const std::unordered_map<Guid, TrackNode, GuidHash>& nodePoses,
    const std::unordered_set<Guid, GuidHash>& keepEdgeSignsNodes) {
    EdgeShapesUnderNodePoses result;
    std::vector<Guid> edgeGuids;
    for (const std::pair<const Guid, TrackNode>& entry : nodePoses) {
        for (const Guid& e : CollectEdgesForNode(graph, entry.first)) {
            if (std::find(edgeGuids.begin(), edgeGuids.end(), e) == edgeGuids.end())
                edgeGuids.push_back(e);
        }
    }
    for (const Guid& e : edgeGuids) {
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator eit = graph.edges.find(e);
        if (eit == graph.edges.end()) continue;
        TrackEdge edge = eit->second;
        const TrackNode* ends[2] = {nullptr, nullptr};
        for (int end = 0; end < 2; ++end) {
            const Guid& g = (end == 0) ? edge.nodeA : edge.nodeB;
            const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator poseIt = nodePoses.find(g);
            if (poseIt != nodePoses.end()) {
                ends[end] = &poseIt->second;
                // MoveNodeCommand と同じ規則で、接線が反対向きになる端の符号を反転して作る。
                const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator curIt =
                    graph.nodes.find(g);
                if (curIt != graph.nodes.end() && keepEdgeSignsNodes.find(g) == keepEdgeSignsNodes.end() &&
                    TangentChangeReversesEdgeDirections(curIt->second.tangent, poseIt->second.tangent)) {
                    if (end == 0) edge.nodeATangentSign *= -1.0f;
                    else edge.nodeBTangentSign *= -1.0f;
                }
            } else {
                const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator nit = graph.nodes.find(g);
                if (nit != graph.nodes.end()) ends[end] = &nit->second;
            }
        }
        if (ends[0] == nullptr || ends[1] == nullptr) {
            result.error = "edgeの端のノードが無い";
            return result;
        }
        EdgeShapeBuild shape = BuildEdgeShape(MakeEdgeShapeInput(edge, *ends[0], *ends[1], {}, &graph));
        if (!shape.ok) {
            result.error = shape.error;
            return result;
        }
        result.edges.push_back({e, std::move(shape)});
    }
    result.ok = true;
    return result;
}

bool CanRebuildEdgesForNode(const TrackGraph& graph, const Guid& nodeGuid,
                            const TrackNode* nodeOverride) {
    std::unordered_map<Guid, TrackNode, GuidHash> poses;
    if (nodeOverride != nullptr) poses[nodeGuid] = *nodeOverride;
    else {
        const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator it = graph.nodes.find(nodeGuid);
        if (it != graph.nodes.end()) poses[nodeGuid] = it->second;
    }
    return BuildEdgeShapesUnderNodePoses(graph, poses).ok;
}

} // namespace rs::world
