#include "WarpTraversal.h"

#include <algorithm>

#include "../../core/Epsilon.h"
#include "RailLinkOverlay.h"

namespace rs::world {

namespace {

// Advance が atEnd=true で停止した地点の端点nodeを特定する。停止位置が
// edge端(s==0 かつ dir<0、または s==edgeLen かつ dir>=0)でなければ
// (=graph不整合等の異常系)nullopt を返す。
std::optional<Guid> EndNodeOfStoppedPosition(const TrackGraph& graph, const TrackPosition& pos) {
    std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt = graph.edges.find(pos.edge);
    if (edgeIt == graph.edges.end()) return std::nullopt;
    const TrackEdge& edge = edgeIt->second;
    const double edgeLen = EdgeLength(edge);
    if (pos.direction >= 0 && pos.s >= edgeLen - rs::core::kPlacementPositionEpsM) return LinkNodeOfEnd(graph, edge, 1);
    if (pos.direction < 0 && pos.s <= rs::core::kPlacementPositionEpsM) return LinkNodeOfEnd(graph, edge, 0);
    return std::nullopt;
}

} // namespace

WarpAdvanceResult AdvanceAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable,
    size_t startScene, TrackPosition start, double ds) {
    WarpAdvanceResult result;
    result.sceneIndex = startScene;
    result.pos = start;

    double remaining = std::max(ds, 0.0);
    // warpホップの反復上限(warpリンクの循環設定等による無限ループ防止)。
    constexpr int kMaxWarpHops = 64;
    bool enteredByWarp = false;

    for (int warpHop = 0; warpHop <= kMaxWarpHops; ++warpHop) {
        const TrackGraph* graph = graphForScene(result.sceneIndex);
        if (graph == nullptr) {
            result.atEnd = true;
            return result;
        }

        AdvanceResult step = Advance(*graph, result.pos, remaining);
        result.pos = step.pos;
        result.consumed += step.consumed;
        remaining -= step.consumed;
        // このstepはresult.sceneIndexが指すgraph上で起きたので、trailing passも
        // 同じsceneIndexで包んで連結する(warpを跨いだ後にsceneIndexが変わっても
        // 過去分のタグ付けは変わらない)。
        for (const TrailingPass& pass : step.trailingPasses)
            result.trailingPasses.push_back(WarpTrailingPass{result.sceneIndex, pass});
        for (size_t edgeIndex = 0; edgeIndex < step.visitedEdges.size(); ++edgeIndex) {
            result.visitedEdges.push_back(WarpVisitedEdge{
                result.sceneIndex, step.visitedEdges[edgeIndex], enteredByWarp && edgeIndex == 0});
        }
        enteredByWarp = false;

        if (!step.atEnd) {
            // ds を消費しきった(warpをまたがずに完了、または直前のwarp後に
            // 残り距離をちょうど使い切った)。
            return result;
        }

        // atEnd: 端点nodeがwarpリンクに登録されていれば相手Sceneへジャンプする。
        std::optional<Guid> endNode = EndNodeOfStoppedPosition(*graph, result.pos);
        if (!endNode.has_value()) {
            result.atEnd = true;
            return result;
        }
        std::optional<WarpEndpoint> other = warpTable.Find(result.sceneIndex, *endNode);
        if (!other.has_value()) {
            result.atEnd = true;
            return result;
        }

        const TrackGraph* otherGraph = graphForScene(other->sceneIndex);
        if (otherGraph == nullptr) {
            result.atEnd = true;
            result.truncatedAtWarp = true;
            return result;
        }
        const TrackNode* node = FindLinkNode(*otherGraph, other->node);
        if (node == nullptr || node->links.empty()) {
            result.atEnd = true;
            result.truncatedAtWarp = true;
            return result;
        }

        // 相手nodeへは「来た edge」を持たないコールドスタートのジャンプなので、
        // Advance内部のポイント選択(SelectNextLink、来た edge を除いて分岐する
        // throat/active判定)はそのままでは適用できない。links の先頭を採用する。
        //
        // これは意図的な設計判断:
        // warp端点は「線路がそこで途切れ、別Sceneへ続く」終端nodeであり、本来
        // links.size()==1 になる。分岐(links.size()>=3)を warp端点に置く運用は
        // 想定しないため、links[0] で十分であり、来たedgeが無い状態に throat/active
        // 判定を拡張しない。
        const TrackNode::Link& nextLink = node->links[0];
        std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator nextEdgeIt =
            otherGraph->edges.find(nextLink.edge);
        if (nextEdgeIt == otherGraph->edges.end()) {
            result.atEnd = true;
            result.truncatedAtWarp = true;
            return result;
        }
        const double nextLen = EdgeLength(nextEdgeIt->second);

        TrackPosition warped;
        warped.edge = nextLink.edge;
        if (nextLink.end == 0) {
            warped.s = 0.0;
            warped.direction = +1;
        } else {
            warped.s = nextLen;
            warped.direction = -1;
        }

        result.sceneIndex = other->sceneIndex;
        result.pos = warped;
        result.atEnd = false;
        if (!result.firstWarpDistance.has_value()) result.firstWarpDistance = result.consumed;
        result.warpHops += 1;
        enteredByWarp = true;

        if (remaining <= rs::core::kZeroLengthEps) {
            // 残りが無くても、ワープ先のedgeにいることは記録に残す(先頭edgeと列の先頭を合わせる)。
            result.visitedEdges.push_back(
                WarpVisitedEdge{result.sceneIndex, VisitedEdge{warped.edge, warped.direction}, true});
            return result;
        }
    }

    // warpホップ反復上限に達した: これ以上進めないとみなす。
    result.atEnd = true;
    return result;
}

WarpAdvanceResult ChaseBackwardAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable,
    size_t startScene, TrackPosition start, double distance) {
    // rs::sim::ChaseBackward と同じ手口: dir を反転して前進走査し、結果の
    // dir を戻す(TrainVehicle.h参照)。
    TrackPosition flipped = start;
    flipped.direction = -flipped.direction;
    WarpAdvanceResult result = AdvanceAcrossScenes(graphForScene, warpTable, startScene, flipped, distance);
    result.pos.direction = -result.pos.direction;
    // 背向(trailing)判定は前進するheadの走査でのみ意味を持つため、後方chase
    // からは常に空で返す(ヘッダのコメント参照)。
    result.trailingPasses.clear();
    result.visitedEdges.clear();
    return result;
}

} // namespace rs::world
