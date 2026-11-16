//
// EdgeMerge.cpp - edge結合(CheckEdgeMerge / BuildEdgeMerge)と結合アンカーの写像の実装
//
// 結合後の形は curve を連結せず、遠端2ノードの3D姿勢から BuildEdgeShape が作る。
//
#include "EdgeSplitMerge.h"

#include "../../core/Epsilon.h"

#include <algorithm>
#include <cmath>

#include "Alignment.h"
#include "AutoCant.h"
#include "CantControlPoints.h"
#include "TrackTraversal.h"
#include "EdgeShapeBuild.h"

namespace rs::world {

namespace {

// curveのセグメント順序を逆にし、各セグメントの向き依存量(Arc.radius/Transitionの
// radius・radiusEnd/grade)を符号反転する。
std::vector<AlignmentSegment> ReverseCurve(const std::vector<AlignmentSegment>& curve) {
    std::vector<AlignmentSegment> reversed;
    reversed.reserve(curve.size());
    for (std::vector<AlignmentSegment>::const_reverse_iterator it = curve.rbegin();
         it != curve.rend(); ++it) {
        AlignmentSegment seg = *it;
        if (seg.kind == AlignmentSegment::Kind::Arc) {
            seg.radius = -seg.radius;
        } else if (seg.kind == AlignmentSegment::Kind::Transition) {
            const float oldRadius = seg.radius;
            const float oldRadiusEnd = seg.radiusEnd;
            seg.radius = -oldRadiusEnd;
            seg.radiusEnd = -oldRadius;
        }
        seg.grade = -seg.grade;
        reversed.push_back(seg);
    }
    return reversed;
}

// 反転側のnodeATangentSignをsplineの接線から決める。originalEndは反転前のedgeが共有nodeで
// 接続していた端(0=nodeA/1=nodeB)。farNodeは反転後の遠端(=共有していない元の端点)。
float ComputeReversedNodeATangentSign(const TrackEdge& originalEdge, int originalEnd,
                               const TrackNode& farNode) {
    rs::core::Vec3 farTangent(0.0f, 0.0f, 1.0f);
    if (!originalEdge.spline.empty()) {
        farTangent = (originalEnd == 0) ? originalEdge.spline.back().tangent
                                         : originalEdge.spline.front().tangent;
    }
    const rs::core::Vec3 desired = farTangent * -1.0f;
    return EdgeTangentSignAgainst(farNode.tangent, desired);
}

// 結合で後半役を反転したとき、反転後の終点(=元のnodeA)側のnodeBTangentSignをsplineの接線から決める。
// 反転後の終点でのedge進行方向は元edgeの始点進行方向(spline先頭の接線)の逆。
float ComputeReversedEndTangentSign(const TrackEdge& originalEdge, const TrackNode& farNode) {
    rs::core::Vec3 startDir(0.0f, 0.0f, 1.0f);
    if (!originalEdge.spline.empty()) startDir = originalEdge.spline.front().tangent;
    return EdgeTangentSignAgainst(farNode.tangent, startDir * -1.0f);
}

// 前半役(edgeFirst)/後半役(edgeSecond)の役割・反転有無・カント・遠端nodeをまとめて
// 導出する(CheckEdgeMergeのカント連続チェックとBuildEdgeMergeの両方から使う)。
struct MergePlan {
    bool frontReversed = false;
    bool backReversed = false;
    double lenFirst = 0.0;
    double lenSecond = 0.0;
    float mergedCantA = 0.0f;
    float mergedCantB = 0.0f;
    //どちらかが制御点を持つとき、両edgeの有効なカント(spline上の値)を連結した制御点列。
    //どちらも持たなければ空で、端のカントだけで結合する。
    std::vector<CantControlPoint> mergedCantPoints;
    //両方がカント自動モードで最大カントが同じときだけ、結合後も自動モードを引き継ぐ。
    //それ以外は自動をやめ、結合前の有効値(mergedCantA/B)を手動値として固定する。
    bool mergedAuto = false;
    float mergedAutoMax = 0.0f;
    Guid frontFarNode = NilGuid();
    Guid backFarNode = NilGuid();
};

MergePlan ComputeMergePlan(const TrackGraph& graph, const TrackEdge& edgeFirst,
                            const TrackNode::Link& linkFirst, const TrackEdge& edgeSecond,
                            const TrackNode::Link& linkSecond) {
    MergePlan plan;
    plan.frontReversed = (linkFirst.end == 0);
    plan.backReversed = (linkSecond.end == 1);
    plan.lenFirst = EdgeLength(edgeFirst);
    plan.lenSecond = EdgeLength(edgeSecond);

    const AutoCantValues firstCant = EffectiveEdgeCant(graph, edgeFirst);
    const AutoCantValues secondCant = EffectiveEdgeCant(graph, edgeSecond);
    const float frontCantA = plan.frontReversed ? firstCant.cantB : firstCant.cantA;
    const float backCantB = plan.backReversed ? secondCant.cantA : secondCant.cantB;
    plan.mergedAuto = edgeFirst.cantAuto && edgeSecond.cantAuto &&
                      edgeFirst.cantAutoMax == edgeSecond.cantAutoMax;
    plan.mergedAutoMax = plan.mergedAuto ? edgeFirst.cantAutoMax : 0.0f;
    plan.mergedCantA = frontCantA;
    plan.mergedCantB = backCantB;
    if (!edgeFirst.cantPoints.empty() || !edgeSecond.cantPoints.empty()) {
        std::vector<CantControlPoint> dense;
        AppendSplineCantSamples(dense, edgeFirst, plan.frontReversed, 0.0f);
        AppendSplineCantSamples(dense, edgeSecond, plan.backReversed, static_cast<float>(plan.lenFirst));
        plan.mergedCantPoints = ThinCantSamples(dense);
    }

    plan.frontFarNode = (linkFirst.end == 0) ? edgeFirst.nodeB : edgeFirst.nodeA;
    plan.backFarNode = (linkSecond.end == 0) ? edgeSecond.nodeB : edgeSecond.nodeA;
    return plan;
}

// 結合後の edge の骨格(遠端2ノード・端の符号・カント。curve/spline/区間・個体は空か親のまま)。
// 遠端ノードが graph に無ければ nullopt。形の確認(CheckEdgeMerge)と組み立て
// (BuildEdgeMerge)が同じ骨格を使う。
std::optional<TrackEdge> MakeMergedEdgeSkeleton(const TrackGraph& graph, const MergePlan& plan,
                                                const TrackNode::Link& linkFirst,
                                                const TrackEdge& edgeFirst,
                                                const TrackEdge& edgeSecond) {
    float mergedNodeATangentSign = edgeFirst.nodeATangentSign;
    if (plan.frontReversed) {
        const auto farIt = graph.nodes.find(plan.frontFarNode);
        if (farIt == graph.nodes.end()) return std::nullopt;
        mergedNodeATangentSign =
            ComputeReversedNodeATangentSign(edgeFirst, linkFirst.end, farIt->second);
    }

    // 結合後の終点は後半役の遠端。反転しない場合は元のnodeBTangentSignをそのまま使い、
    // 反転する場合(遠端=後半役のnodeA)は始点側のedge進行方向の逆向きから決める。
    float mergedNodeBTangentSign = edgeSecond.nodeBTangentSign;
    if (plan.backReversed) {
        const auto farIt = graph.nodes.find(plan.backFarNode);
        if (farIt == graph.nodes.end()) return std::nullopt;
        mergedNodeBTangentSign = ComputeReversedEndTangentSign(edgeSecond, farIt->second);
    }
    if (graph.nodes.find(plan.frontFarNode) == graph.nodes.end() ||
        graph.nodes.find(plan.backFarNode) == graph.nodes.end())
        return std::nullopt;

    TrackEdge merged = edgeFirst;
    merged.nodeA = plan.frontFarNode;
    merged.nodeB = plan.backFarNode;
    merged.curve.clear();
    merged.spline.clear();  // 呼び出し側のRebuildEdgeSplineが再生成する
    merged.cantA = plan.mergedCantA;
    merged.cantB = plan.mergedCantB;
    merged.cantMid = 0.0f;
    merged.cantPoints = plan.mergedAuto ? std::vector<CantControlPoint>{} : plan.mergedCantPoints;
    merged.cantTransitionRatio = 0.0f;
    merged.cantTransitionShape = ::rs::world::AlignmentSegment::TransitionShape::Clothoid;
    merged.cantAuto = plan.mergedAuto;
    merged.cantAutoMax = plan.mergedAutoMax;
    merged.refit = TrackEdge::Refit{};
    merged.nodeATangentSign = mergedNodeATangentSign;
    merged.nodeBTangentSign = mergedNodeBTangentSign;
    merged.layer = edgeFirst.layer;
    merged.centerlineOnly = false;
    merged.trackCount = 0;
    merged.trackSpacing = 0.0f;
    merged.trackEdges.clear();
    merged.detachedFromCenterline = false;
    return merged;
}

}  // namespace

EdgeMergeRefusal CheckEdgeMerge(const TrackGraph& graph, const Guid& sharedNodeGuid,
                                 const WarpLinkTable* warpLinks, bool allowLayerMismatch) {
    const auto nodeIt = graph.nodes.find(sharedNodeGuid);
    if (nodeIt == graph.nodes.end()) return EdgeMergeRefusal::NotDegree2;
    const TrackNode& sharedNode = nodeIt->second;
    if (sharedNode.links.size() != 2) return EdgeMergeRefusal::NotDegree2;
    if (sharedNode.IsPoint()) return EdgeMergeRefusal::IsBranchingNode;

    const TrackNode::Link& linkFirst = sharedNode.links[0];
    const TrackNode::Link& linkSecond = sharedNode.links[1];
    const auto itFirst = graph.edges.find(linkFirst.edge);
    const auto itSecond = graph.edges.find(linkSecond.edge);
    if (itFirst == graph.edges.end() || itSecond == graph.edges.end())
        return EdgeMergeRefusal::NotDegree2;
    const TrackEdge& edgeFirst = itFirst->second;
    const TrackEdge& edgeSecond = itSecond->second;

    if (edgeFirst.curve.empty() || edgeSecond.curve.empty()) return EdgeMergeRefusal::NoCurve;

    // 結合は片方のedgeを消して端を付け替えるので、継ぎ目の接続側ノードを持ち越せない。
    for (const TrackEdge* edge : {&edgeFirst, &edgeSecond}) {
        if (!GuidIsNil(edge->linkNodes[0]) || !GuidIsNil(edge->linkNodes[1]))
            return EdgeMergeRefusal::HasSeam;
    }

    // 複線中心線グループ(centerlineOnly edge自身、およびtrackEdgesメンバー)の結合は
    // ここでは拒否しない。単一edgeの結合可否判定である本関数はグループ関係を問わず、
    // グループ全体の同期結合はEditor層(EditorCenterlineMerge.cpp::SyncMergeCenterlineGroupAt)
    // が担う。同期結合はグループ内の全nodeへ本関数を個別に適用して可否を
    // 検証するため、centerlineOnly/trackEdges固有の拒否はここに残さない。

    if (!allowLayerMismatch && edgeFirst.layer != edgeSecond.layer)
        return EdgeMergeRefusal::LayerMismatch;

    // 2本の遠端が同じnodeだと、結合後は1本のedgeが同じnodeへ両端で刺さる自己ループに
    // なる。そのnodeには同じedge guidのlinkがend=0/1で2本ぶら下がるが、SelectNextLink
    // は cameFromEdge に最初に一致したlinkしか見ないため、走行がおかしくなる。
    const Guid farFirst = (linkFirst.end == 0) ? edgeFirst.nodeB : edgeFirst.nodeA;
    const Guid farSecond = (linkSecond.end == 0) ? edgeSecond.nodeB : edgeSecond.nodeA;
    if (farFirst == farSecond) return EdgeMergeRefusal::FormsLoop;

    if (warpLinks != nullptr) {
        for (const WarpLink& link : warpLinks->Links()) {
            if (link.a.node == sharedNodeGuid || link.b.node == sharedNodeGuid)
                return EdgeMergeRefusal::WarpOnSharedNode;
        }
    }

    const MergePlan plan = ComputeMergePlan(graph, edgeFirst, linkFirst, edgeSecond, linkSecond);

    // 結合点(前半役の終端=後半役の始端)で曲率が不連続でないか判定する。
    // frontReversed/backReversedを反映した向き(BuildEdgeMergeが実際に連結する向き)で
    // 前半curveの末尾セグメント終端曲率・後半curveの先頭セグメント始端曲率を比較する
    // (CantDiscontinuousと同じ設計)。判定対象は
    // 「結合点にTransitionが関与する場合」のみに限る。Transitionを介さないArc同士
    // (異なる半径の複合曲線、Arc-Line等)は無制限に結合できるので、
    // そこに新規の拒否を持ち込まないため。
    const std::vector<AlignmentSegment> frontCurveForCheck =
        plan.frontReversed ? ReverseCurve(edgeFirst.curve) : edgeFirst.curve;
    const std::vector<AlignmentSegment> backCurveForCheck =
        plan.backReversed ? ReverseCurve(edgeSecond.curve) : edgeSecond.curve;
    if (!frontCurveForCheck.empty() && !backCurveForCheck.empty()) {
        const AlignmentSegment& lastSeg = frontCurveForCheck.back();
        const AlignmentSegment& firstSeg = backCurveForCheck.front();
        if (lastSeg.kind == AlignmentSegment::Kind::Transition ||
            firstSeg.kind == AlignmentSegment::Kind::Transition) {
            const float endCurvature = (lastSeg.kind == AlignmentSegment::Kind::Transition)
                                            ? CurvatureFromRadius(lastSeg.radiusEnd)
                                            : CurvatureFromRadius(lastSeg.radius);
            const float startCurvature = CurvatureFromRadius(firstSeg.radius);
            constexpr float kCurvatureContinuityTolerance = rs::core::kCurvatureContinuityEpsPerM;  // [1/m]
            if (std::abs(endCurvature - startCurvature) > kCurvatureContinuityTolerance)
                return EdgeMergeRefusal::CurvatureDiscontinuous;
        }
    }

    const double lenTotal = plan.lenFirst + plan.lenSecond;
    //自動モードの edge が絡むときは共有ノードの cant を保存していないので、この判定は行わない。
    const bool anyAuto = edgeFirst.cantAuto || edgeSecond.cantAuto;
    if (lenTotal > kMinSpanLen && !anyAuto) {
        // 結合後のカントが折れ線なら、その折れ線の共有点での値が期待値。
        const float expectedSharedCant = CantAtControlPoints(
            plan.mergedCantPoints, static_cast<float>(plan.lenFirst), plan.mergedCantA,
            plan.mergedCantB, static_cast<float>(lenTotal));
        if (std::abs(sharedNode.cant - expectedSharedCant) > 0.01f)
            return EdgeMergeRefusal::CantDiscontinuous;
    }

    // 結合後の形を、遠端2ノードの姿勢から作れること。
    const std::optional<TrackEdge> skeleton =
        MakeMergedEdgeSkeleton(graph, plan, linkFirst, edgeFirst, edgeSecond);
    if (!skeleton.has_value()) return EdgeMergeRefusal::ShapeUnsolvable;
    if (!BuildEdgeShape(MakeEdgeShapeInput(*skeleton, graph.nodes.at(plan.frontFarNode),
                                           graph.nodes.at(plan.backFarNode)))
             .ok)
        return EdgeMergeRefusal::ShapeUnsolvable;

    return EdgeMergeRefusal::None;
}

std::optional<EdgeMergeResult> BuildEdgeMerge(const TrackGraph& graph,
                                               const Guid& sharedNodeGuid,
                                               bool allowLayerMismatch) {
    if (CheckEdgeMerge(graph, sharedNodeGuid, nullptr, allowLayerMismatch) !=
        EdgeMergeRefusal::None)
        return std::nullopt;

    const TrackNode& sharedNode = graph.nodes.at(sharedNodeGuid);
    const TrackNode::Link& linkFirst = sharedNode.links[0];
    const TrackNode::Link& linkSecond = sharedNode.links[1];
    const TrackEdge& edgeFirst = graph.edges.at(linkFirst.edge);
    const TrackEdge& edgeSecond = graph.edges.at(linkSecond.edge);

    const MergePlan plan = ComputeMergePlan(graph, edgeFirst, linkFirst, edgeSecond, linkSecond);

    std::optional<TrackEdge> skeleton =
        MakeMergedEdgeSkeleton(graph, plan, linkFirst, edgeFirst, edgeSecond);
    if (!skeleton.has_value()) return std::nullopt;
    TrackEdge merged = std::move(*skeleton);

    // IntervalList: 反転側はReversedCopyでs反転してからAppendFrom。
    const IntervalList<PluginSet> frontPlugin =
        plan.frontReversed ? edgeFirst.pluginSections.ReversedCopy(plan.lenFirst)
                            : edgeFirst.pluginSections;
    const IntervalList<PluginSet> backPlugin =
        plan.backReversed ? edgeSecond.pluginSections.ReversedCopy(plan.lenSecond)
                           : edgeSecond.pluginSections;
    merged.pluginSections = frontPlugin;
    merged.pluginSections.AppendFrom(backPlugin, plan.lenFirst);

    // 速度制限は1値。長いほうのレールの値にする(同じ長さなら前半役)。
    const bool firstWins = plan.lenFirst >= plan.lenSecond;
    merged.speedLimitKmh = (firstWins ? edgeFirst : edgeSecond).speedLimitKmh;

    EdgeMergeResult result;
    result.firstWins = firstWins;
    result.merged = std::move(merged);
    result.removedEdgeGuid = edgeSecond.guid;
    result.removedNodeGuid = sharedNodeGuid;
    result.lenFirst = plan.lenFirst;
    result.lenSecond = plan.lenSecond;
    return result;
}

bool RemapMergedAnchor(const EdgeMergeRemap& remap, Guid& edge, double& s, bool* outReversed) {
    if (edge == remap.keptEdge) {
        if (remap.keptReversed) s = remap.lenKept - s;
        if (outReversed != nullptr) *outReversed = remap.keptReversed;
        return true;
    }
    if (edge == remap.removedEdge) {
        s = (remap.removedReversed ? (remap.lenRemoved - s) : s) + remap.lenKept;
        edge = remap.keptEdge;
        if (outReversed != nullptr) *outReversed = remap.removedReversed;
        return true;
    }
    return false;
}

bool UnremapMergedAnchor(const EdgeMergeRemap& remap, Guid& edge, double& s, bool* outReversed) {
    if (edge != remap.keptEdge) return false;
    if (s < remap.lenKept) {
        if (remap.keptReversed) s = remap.lenKept - s;
        if (outReversed != nullptr) *outReversed = remap.keptReversed;
        return true;
    }
    const double t = s - remap.lenKept;
    s = remap.removedReversed ? (remap.lenRemoved - t) : t;
    edge = remap.removedEdge;
    if (outReversed != nullptr) *outReversed = remap.removedReversed;
    return true;
}

}  // namespace rs::world
