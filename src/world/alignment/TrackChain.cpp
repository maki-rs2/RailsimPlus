//
// TrackChain.cpp
//
#include "TrackChain.h"
#include "RailLinkOverlay.h"

#include <algorithm>
#include <optional>
#include <unordered_set>

namespace rs::world {

namespace {

// atNodeにおいて、cameFromEdge以外のlinkを返す(atNodeがlinks.size()==2の
// 通過点でない場合はnullopt=チェーン終端)。
struct RawStep {
    Guid edge = NilGuid();
    int endAtAtNode = 0; // atNodeがこのedgeのどちら側(0:nodeA/1:nodeB)か
};

std::optional<RawStep> NextRaw(const TrackGraph& graph, const Guid& atNode,
                               const Guid& cameFromEdge) {
    const TrackNode* nodePtr = FindLinkNode(graph, atNode);
    if (nodePtr == nullptr) return std::nullopt;
    const TrackNode& node = *nodePtr;
    if (node.links.size() != 2) return std::nullopt;
    for (const TrackNode::Link& link : node.links) {
        if (link.edge == cameFromEdge) continue;
        // .rs2取り込みではAddressは有るが未解決(dangling)なリンクがedge=NilGuidの
        // まま links に残る(Point index整合のため)。
        // 存在しないedgeはチェーン終端として扱う。
        if (GuidIsNil(link.edge) || graph.edges.find(link.edge) == graph.edges.end())
            return std::nullopt;
        return RawStep{link.edge, link.end};
    }
    return std::nullopt;
}

Guid FarEndOf(const TrackGraph& graph, const TrackEdge& edge, int endAtAtNode) {
    return LinkNodeOfEnd(graph, edge, endAtAtNode == 0 ? 1 : 0);
}

// atNodeから伸びる方向へチェーンを辿りoutStepsへ積む。dirSign=+1は前方向
// (nodeB側)伸長と同じ符号規約、-1は後方向(nodeA側、atNodeへ流れ込む向き)の
// 符号反転を表す。visited/isLoop検出は前後方向共通のロジックのため、ここへ
// 集約する。戻り値はループ検出でtrueになったかどうか。
bool WalkChainDirection(const TrackGraph& graph, Guid atNode, const Guid& startEdge, int dirSign,
                        std::unordered_set<Guid, GuidHash>& visited,
                        std::vector<EdgeChainStep>& outSteps) {
    Guid cameFrom = startEdge;
    while (true) {
        std::optional<RawStep> raw = NextRaw(graph, atNode, cameFrom);
        if (!raw) return false;
        if (visited.count(raw->edge)) return true;
        const int dir = (raw->endAtAtNode == 0 ? +1 : -1) * dirSign;
        outSteps.push_back({raw->edge, dir});
        visited.insert(raw->edge);
        const TrackEdge& ne = graph.edges.at(raw->edge);
        atNode = FarEndOf(graph, ne, raw->endAtAtNode);
        cameFrom = raw->edge;
    }
}

} // namespace

std::vector<EdgeChain> BuildEdgeChains(const TrackGraph& graph) {
    std::vector<Guid> sortedEdges;
    sortedEdges.reserve(graph.edges.size());
    for (const auto& kv : graph.edges) {
        if (IsCenterlineOnlyEdge(kv.second)) continue;
        sortedEdges.push_back(kv.first);
    }
    std::sort(sortedEdges.begin(), sortedEdges.end(), [](const Guid& a, const Guid& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
    });

    std::vector<EdgeChain> chains;
    std::unordered_set<Guid, GuidHash> visited;

    for (const Guid& startEdge : sortedEdges) {
        if (visited.count(startEdge)) continue;
        visited.insert(startEdge);
        const TrackEdge& se = graph.edges.at(startEdge);

        EdgeChain chain;
        chain.steps.push_back({startEdge, +1});

        // 前方向(nodeB側)への伸長。
        chain.isLoop = WalkChainDirection(graph, LinkNodeOfEnd(graph, se, 1), startEdge, +1, visited, chain.steps);

        // 後方向(nodeA側)への伸長。チェーン先頭へ差し込むため反転して連結する。
        if (!chain.isLoop) {
            std::vector<EdgeChainStep> backSteps;
            chain.isLoop = WalkChainDirection(graph, LinkNodeOfEnd(graph, se, 0), startEdge, -1, visited, backSteps);
            std::reverse(backSteps.begin(), backSteps.end());
            chain.steps.insert(chain.steps.begin(), backSteps.begin(), backSteps.end());
        }

        chains.push_back(std::move(chain));
    }
    return chains;
}

} // namespace rs::world
