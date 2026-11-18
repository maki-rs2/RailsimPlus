//
// EdgeSplit.cpp - edge分割(CheckEdgeSplit / BuildEdgeSplit)の実装
//
// 分割後の前半・後半の形は curve を割らず、中間ノードの3D姿勢(分割位置の spline の
// 位置・ピッチ込みの接線・up)と両端ノードの姿勢から BuildEdgeShape が作る。
// 事前確認(CheckEdgeSplit)と組み立て(BuildEdgeSplit)は同じ下書きを使う。
//
#include "EdgeSplitMerge.h"

#include <utility>

#include "Alignment.h"
#include "CantControlPoints.h"
#include "EdgeShapeBuild.h"
#include "TrackTraversal.h"
#include "TrackGraph.h"

namespace rs::world {

TrackNode MakeSplitMiddleNode(const TrackEdge& edge, double s, const Guid& guid) {
    const TrackFrame frame = EvaluateAt(edge, s, +1);
    TrackNode middle;
    middle.guid = guid;
    middle.pos = frame.pos;
    middle.tangent = frame.tangent;
    middle.up = frame.up;
    middle.cant = frame.cant;
    return middle;
}

namespace {

// 分割の下書き。前半・後半の形を作れることまで確かめる(curve/spline は書かない)。
struct SplitDraft {
    EdgeSplitRefusal refusal = EdgeSplitRefusal::None;
    EdgeSplitResult result;
};

SplitDraft DraftEdgeSplit(const TrackGraph& graph, const TrackEdge& edge, double s,
                          const Guid& newEdgeGuid, const Guid& newNodeGuid) {
    SplitDraft draft;
    if (edge.spline.empty()) {
        draft.refusal = EdgeSplitRefusal::OutOfRange;
        return draft;
    }
    // 継ぎ目の端は、形の基準(nodeA/nodeB)と接続(linkNodes)が別ノード。分割は片方の端を
    // 新しいedgeへ移すので、接続側の持ち主が分からなくなる。
    if (!GuidIsNil(edge.linkNodes[0]) || !GuidIsNil(edge.linkNodes[1])) {
        draft.refusal = EdgeSplitRefusal::HasSeam;
        return draft;
    }
    const double totalLen = static_cast<double>(edge.spline.back().s);
    if (s < kMinSpanLen || s > totalLen - kMinSpanLen) {
        draft.refusal = EdgeSplitRefusal::OutOfRange;
        return draft;
    }

    // 複線中心線グループ(centerlineOnly edge自身、およびtrackEdgesメンバー)の分割は
    // ここでは拒否しない。単一edgeの分割可否判定である本関数はグループ関係を問わず、
    // グループ全体の同期分割はEditor層(EditorCenterlineSplit.cpp::SplitCenterlineGroupAt)
    // が担う。

    const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator itA =
        graph.nodes.find(edge.nodeA);
    const std::unordered_map<Guid, TrackNode, GuidHash>::const_iterator itB =
        graph.nodes.find(edge.nodeB);
    if (itA == graph.nodes.end() || itB == graph.nodes.end()) {
        draft.refusal = EdgeSplitRefusal::ShapeUnsolvable;
        return draft;
    }

    const TrackNode middle = MakeSplitMiddleNode(edge, s, newNodeGuid);

    // 前半・後半は緩和曲線の指定なし(refit は既定値)で、姿勢だけから作る。
    TrackEdge first = edge;
    first.nodeB = newNodeGuid;
    first.curve.clear();
    first.spline.clear();  // 呼び出し側のRebuildEdgeSplineが再生成する。親の全長分のsplineを
                           // 残すと内部のRescaleEdgeSections(oldLen=親の全長)が誤って区間を縮める
    first.cantB = middle.cant;
    first.nodeBTangentSign = 1.0f;  // 終点はmiddle(接線=分割点の接線そのもの)
    first.refit = TrackEdge::Refit{};
    // 制御点は弧長で振り分ける(分割点のカントは両端の cantB/cantA が持つ)。
    std::pair<std::vector<CantControlPoint>, std::vector<CantControlPoint>> cantHalves =
        SplitCantControlPoints(edge.cantPoints, static_cast<float>(s));
    first.cantPoints = std::move(cantHalves.first);

    TrackEdge second = edge;
    second.guid = newEdgeGuid;
    second.nodeA = newNodeGuid;
    second.curve.clear();
    second.spline.clear();
    second.cantA = middle.cant;
    second.nodeATangentSign = 1.0f;  // middle.tangentを分割点の接線そのものに設定しているため
    second.refit = TrackEdge::Refit{};
    second.cantPoints = std::move(cantHalves.second);

    if (!BuildEdgeShape(MakeEdgeShapeInput(first, itA->second, middle)).ok ||
        !BuildEdgeShape(MakeEdgeShapeInput(second, middle, itB->second)).ok) {
        draft.refusal = EdgeSplitRefusal::ShapeUnsolvable;
        return draft;
    }

    draft.result.first = std::move(first);
    draft.result.second = std::move(second);
    draft.result.middle = middle;
    return draft;
}

}  // namespace

EdgeSplitRefusal CheckEdgeSplit(const TrackGraph& graph, const TrackEdge& edge, double s) {
    return DraftEdgeSplit(graph, edge, s, NilGuid(), NilGuid()).refusal;
}

std::optional<EdgeSplitResult> BuildEdgeSplit(const TrackGraph& graph, const TrackEdge& edge,
                                               double s, const Guid& newEdgeGuid,
                                               const Guid& newNodeGuid) {
    SplitDraft draft = DraftEdgeSplit(graph, edge, s, newEdgeGuid, newNodeGuid);
    if (draft.refusal != EdgeSplitRefusal::None) return std::nullopt;
    EdgeSplitResult& result = draft.result;
    // links は呼び出し側(SplitEdgeCommand)が張る(前半side0/後半side1)。

    // 区間表の分配(SplitOffがfirstを前半のまま残し後半をsecondへ切り出す)。
    // 速度制限は1値なので、2本とも元の値を受け継ぐ(Draftがedgeを写している前提)。
    result.second.pluginSections = result.first.pluginSections.SplitOff(s);
    result.second.speedLimitKmh = result.first.speedLimitKmh;
    return std::move(result);
}

}  // namespace rs::world
