#include "TrackTraversal.h"

#include <algorithm>
#include <cmath>

#include "SplineSampleBracket.h"
#include "RailLinkOverlay.h"
#include "TrackEdge.h"
#include "TrackGraph.h"

namespace rs::world {
namespace {

// 出口面のexitEndから見て「最初の分岐点」とみなす最小距離[m](RailSim II の POINT_DEC_MIN と同値)。
constexpr double kPointDecisionMinM = 5.0;

// edgeのnode側端点(end: 0=nodeA側/1=nodeB側)から、そのnodeから見て外向きに
// kPointDecisionMinM以上離れた最初のspline点までのベクトルを返す
// (RailSim II の CRailWay::GetFirstDir と同じ規則)。spline点は起点からの累積距離sを既に
// 持つ(TrackNodeSample::s)ので、区間長を再計算せずそれを使う。
// edge.splineが2点未満(縮退)なら零ベクトル。
rs::core::Vec3 OutwardDirFromNode(const TrackEdge& edge, int end) {
    const size_t count = edge.spline.size();
    if (count < 2) return rs::core::Vec3(0, 0, 0);
    const size_t startIdx = end == 0 ? 0 : count - 1;
    const rs::core::Vec3 first = edge.spline[startIdx].pos;
    const double totalLength = static_cast<double>(edge.spline.back().s);
    rs::core::Vec3 dir(0, 0, 0);
    for (size_t step = 1; step < count; ++step) {
        const size_t idx = end == 0 ? step : count - 1 - step;
        const double distanceFromNode =
            end == 0 ? static_cast<double>(edge.spline[idx].s)
                     : totalLength - static_cast<double>(edge.spline[idx].s);
        dir = edge.spline[idx].pos - first;
        if (distanceFromNode >= kPointDecisionMinM) break;
    }
    return dir;
}

}  // namespace

double EdgeLength(const TrackEdge& edge) {
    return edge.spline.empty() ? 0.0 : static_cast<double>(edge.spline.back().s);
}

// 戻り値 next=nullopt = 行き止まり(出口面に候補が無い、または来た edge がこの
// node に繋がっていない)。
LinkSelection SelectNextLink(const TrackNode& node, const Guid& cameFromEdge,
                             const NodeSlotOverride* slotOverride) {
    LinkSelection selection;
    for (int i = 0; i < static_cast<int>(node.links.size()); ++i) {
        if (node.links[static_cast<size_t>(i)].edge == cameFromEdge) {
            selection.enteredIndex = i;
            break;
        }
    }
    if (selection.enteredIndex < 0) return selection;  // 来た edge がこの node に繋がっていない

    const int exitSide = 1 - node.links[static_cast<size_t>(selection.enteredIndex)].side;
    const int count = node.LinkCountOnSide(exitSide);
    if (count == 0) return selection;
    if (count == 1) {
        selection.next = node.links[static_cast<size_t>(node.LinkIndexOnSide(exitSide, 0))];
        return selection;
    }

    int slot = node.activeSlot[static_cast<size_t>(exitSide)];
    if (slotOverride != nullptr) {
        const NodeSlotOverride::const_iterator overrideIt = slotOverride->find(node.guid);
        if (overrideIt != slotOverride->end() &&
            overrideIt->second[static_cast<size_t>(exitSide)].has_value()) {
            slot = *overrideIt->second[static_cast<size_t>(exitSide)];
        }
    }
    slot = std::clamp(slot, 0, count - 1);
    selection.next = node.links[static_cast<size_t>(node.LinkIndexOnSide(exitSide, slot))];
    return selection;
}

// SelectNextLink の結果が仮想edge(複線中心線ダミー等)を指す場合は、列車が
// 仮想edgeへ乗らないよう選択自体を無効化する(行き止まり扱い)。enteredIndex は
// 選択の成否によらずそのまま引き継ぐ(トレイリング判定は選択結果と独立のため)。
LinkSelection SelectNextRealLink(const TrackGraph& graph, const TrackNode& node,
                                 const Guid& cameFromEdge, const NodeSlotOverride* slotOverride) {
    LinkSelection selection = SelectNextLink(node, cameFromEdge, slotOverride);
    if (!selection.next.has_value()) return selection;
    auto edgeIt = graph.edges.find(selection.next->edge);
    if (edgeIt != graph.edges.end() && IsCenterlineOnlyEdge(edgeIt->second)) selection.next.reset();
    return selection;
}

TrackFrame EvaluateAt(const TrackEdge& edge, double s, int direction) {
    TrackFrame frame = EvaluateAtRest(edge, s, direction);
    // 親に追従するedgeだけ、休止姿勢のフレームへ剛体差分を掛ける(弧長 s は変わらない)。
    if (IsParentDrivenEdge(edge) && !edge.parentDelta.IsIdentity()) {
        const auto toVec3 = [](const rs::core::DVec3& v) {
            return rs::core::Vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
        };
        const auto toD = [](const rs::core::Vec3& v) {
            return rs::core::DVec3{v.x, v.y, v.z};
        };
        frame.pos = toVec3(ApplyToPoint(edge.parentDelta, toD(frame.pos)));
        frame.tangent = toVec3(ApplyToDirection(edge.parentDelta, toD(frame.tangent)));
        frame.up = toVec3(ApplyToDirection(edge.parentDelta, toD(frame.up)));
    }
    return frame;
}

TrackFrame EvaluateAtRest(const TrackEdge& edge, double s, int direction) {
    TrackFrame frame;
    if (edge.spline.size() < 2) {
        frame.tangent = rs::core::Vec3(0, 0, 1);
        frame.up = rs::core::Vec3(0, 1, 0);
        return frame;
    }

    const double edgeLen = EdgeLength(edge);
    const double sc = std::clamp(s, 0.0, edgeLen);

    const SplineBracket bracket = LocateSplineBracketBinary(edge.spline, sc);
    const TrackNodeSample& a = edge.spline[bracket.lowerIndex];
    const TrackNodeSample& b = edge.spline[bracket.lowerIndex + 1];
    const float t = bracket.t;

    frame.pos = a.pos + (b.pos - a.pos) * t;
    frame.tangent = (a.tangent + (b.tangent - a.tangent) * t).Normalized();
    frame.up = (a.up + (b.up - a.up) * t).Normalized();
    frame.cant = std::lerp(a.cant, b.cant, t);

    if (direction < 0) frame.tangent = frame.tangent * -1.0f;
    return frame;
}

int ResolveHandToOrdinal(const TrackGraph& graph, const TrackNode& node, int exitSide, int hand) {
    const int count = node.LinkCountOnSide(exitSide);
    if (count < 2) return 0;

    rs::core::Vec3 branchDir[2];
    bool branchPresent[2] = {false, false};
    for (int ordinal = 0; ordinal < 2; ++ordinal) {
        const int linkIndex = node.LinkIndexOnSide(exitSide, ordinal);
        if (linkIndex < 0) continue;
        const TrackNode::Link& link = node.links[static_cast<size_t>(linkIndex)];
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
            graph.edges.find(link.edge);
        if (edgeIt == graph.edges.end()) continue;
        branchDir[ordinal] = OutwardDirFromNode(edgeIt->second, link.end);
        branchPresent[ordinal] = true;
    }

    // 分岐器のrightベクトル(RailSim II の CRailSplitter::Read の right = normalize(up × dir))。
    // 進入面(=出口面の反対面)がS1面なら列車から見た左右が反転する
    // (RailSim II の CRailConnectorLink::GetRight)。entrySide = 1 - exitSide。
    const int entrySide = 1 - exitSide;
    rs::core::Vec3 right = rs::core::Vec3::Cross(node.up, node.tangent).Normalized();
    if (entrySide == 1) right = right * -1.0f;

    // hand値は面内スロット番号ではなく左右(0=左/1=右)。既定は「面内スロット0=左」で、
    // 実際にスロット0の方が右へ出ていくなら入れ替える。
    bool swapHands = false;
    if (branchPresent[0] && branchPresent[1]) {
        swapHands = rs::core::Vec3::Dot(right, branchDir[0].Normalized()) >
                    rs::core::Vec3::Dot(right, branchDir[1].Normalized());
    }
    return swapHands ? (1 - hand) : hand;
}

AdvanceResult Advance(const TrackGraph& graph, TrackPosition start, double ds) {
    AdvanceResult result;
    result.pos = start;

    result.visitedEdges.push_back(VisitedEdge{start.edge, start.direction});

    double remaining = std::max(ds, 0.0);
    // node跨ぎの反復上限(零長edgeやグラフ不整合による無限ループ防止)。
    constexpr int kMaxHops = 4096;

    for (int hop = 0; hop <= kMaxHops; ++hop) {
        auto edgeIt = graph.edges.find(result.pos.edge);
        if (edgeIt == graph.edges.end()) {
            result.atEnd = true;
            return result;
        }
        const TrackEdge& edge = edgeIt->second;
        const double edgeLen = EdgeLength(edge);

        if (result.pos.direction >= 0) {
            const double toEnd = edgeLen - result.pos.s;
            if (remaining <= toEnd) {
                result.pos.s += remaining;
                result.consumed += remaining;
                return result;
            }
            result.pos.s = edgeLen;
            result.consumed += toEnd;
            remaining -= toEnd;
        } else {
            const double toEnd = result.pos.s;
            if (remaining <= toEnd) {
                result.pos.s -= remaining;
                result.consumed += remaining;
                return result;
            }
            result.pos.s = 0.0;
            result.consumed += toEnd;
            remaining -= toEnd;
        }

        // edge 端に到達: 端点 node を跨いで次 edge へ。
        const Guid sharedNode = LinkNodeOfEnd(graph, edge, result.pos.direction >= 0 ? 1 : 0);
        const TrackNode* node = FindLinkNode(graph, sharedNode);
        if (node == nullptr) {
            result.atEnd = true;
            return result;
        }
        const LinkSelection selection = SelectNextRealLink(graph, *node, result.pos.edge);
        if (!selection.next.has_value()) {
            result.atEnd = true;
            return result;
        }
        const std::optional<TrackNode::Link>& next = selection.next;

        // 背向(trailing)通過の記録: 進入した面(=来た edge の面)が分岐していて、
        // かつ進入スロットが現在の開通スロットと違う場合だけ積む。向かい(facing)通過は進入面が分岐して
        // いないので記録されず、既に開通している脚からの背向進入も状態が変わら
        // ないので記録しない。
        if (selection.enteredIndex >= 0) {
            const int enterSide = node->links[static_cast<size_t>(selection.enteredIndex)].side;
            if (node->IsBranching(enterSide)) {
                const int enterSlot = node->SlotOfLink(selection.enteredIndex);
                if (enterSlot != node->activeSlot[static_cast<size_t>(enterSide)]) {
                    TrailingPass pass;
                    pass.node = sharedNode;
                    pass.side = enterSide;
                    pass.slot = enterSlot;
                    result.trailingPasses.push_back(pass);
                }
            }
        }

        auto nextEdgeIt = graph.edges.find(next->edge);
        if (nextEdgeIt == graph.edges.end()) {
            result.atEnd = true;
            return result;
        }
        const double nextLen = EdgeLength(nextEdgeIt->second);
        result.pos.edge = next->edge;
        if (next->end == 0) {
            result.pos.s = 0.0;
            result.pos.direction = +1;
        } else {
            result.pos.s = nextLen;
            result.pos.direction = -1;
        }
        result.visitedEdges.push_back(VisitedEdge{result.pos.edge, result.pos.direction});
    }

    // 反復上限に達した(零長edgeの連鎖等): これ以上進めないとみなす。
    result.atEnd = true;
    return result;
}

} // namespace rs::world
