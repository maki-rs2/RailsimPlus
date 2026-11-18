#include "RailLinkEdit.h"

#include <algorithm>

#include "RailLinkOverlay.h"
#include "PlatformStopInfo.h"
#include "TrackEdge.h"
#include "TrackGraph.h"
#include "TrackNode.h"

namespace rs::world {
namespace {

// 端が今つながっているノード(重ね置きを含めた実効の姿)と、そのノードの links 内の添字。
struct EndLink {
    const TrackNode* node = nullptr;
    int linkIndex = -1;
    int side = 0;
};

TrackEdge* FindEdge(TrackGraph& graph, const Guid& guid) {
    const auto it = graph.edges.find(guid);
    return it == graph.edges.end() ? nullptr : &it->second;
}

std::optional<EndLink> FindEndLink(const TrackGraph& graph, const TrackEdge& edge, int end) {
    const TrackNode* node = FindLinkNode(graph, LinkNodeOfEnd(graph, edge, end));
    if (node == nullptr) return std::nullopt;
    for (int index = 0; index < static_cast<int>(node->links.size()); ++index) {
        const TrackNode::Link& link = node->links[static_cast<size_t>(index)];
        if (link.edge == edge.guid && link.end == end) return EndLink{node, index, link.side};
    }
    return std::nullopt;
}

bool FaceContainsEdge(const TrackNode& node, int side, const Guid& edge) {
    return std::any_of(node.links.begin(), node.links.end(), [&](const TrackNode::Link& link) {
        return link.side == side && link.edge == edge;
    });
}

// 端の位置・向き・アップ・カントを持つ、links の無い新しいノード。形の基準ノードは静的なまま引く。
TrackNode MakeNodeAtEnd(const TrackGraph& graph, const TrackEdge& edge, int end) {
    TrackNode node;
    node.guid = GenerateGuid();
    const auto anchorIt = graph.nodes.find(end == 0 ? edge.nodeA : edge.nodeB);
    if (anchorIt != graph.nodes.end()) {
        node.pos = anchorIt->second.pos;
        node.tangent = anchorIt->second.tangent;
        node.up = anchorIt->second.up;
        node.cant = anchorIt->second.cant;
    }
    return node;
}

// 1端を今のノードから外す(実効ノードの links から取り除く)。静的ノードは変えない。
void DetachEnd(TrackGraph& graph, const TrackEdge& edge, int end) {
    const std::optional<EndLink> endLink = FindEndLink(graph, edge, end);
    if (!endLink) return;
    TrackNode* node = EnsureEffectiveNode(graph, endLink->node->guid);
    node->links.erase(node->links.begin() + endLink->linkIndex);
}

}  // namespace

bool ConnectEdgeEnds(TrackGraph& graph, const EdgeEnd& a, const EdgeEnd& b, const NodeInUseFn& inUse) {
    TrackEdge* edgeA = FindEdge(graph, a.edge);
    TrackEdge* edgeB = FindEdge(graph, b.edge);
    if (edgeA == nullptr || edgeB == nullptr) return false;
    const std::optional<EndLink> linkA = FindEndLink(graph, *edgeA, a.end);
    const std::optional<EndLink> linkB = FindEndLink(graph, *edgeB, b.end);
    if (!linkA || !linkB) return false;
    if (FaceContainsEdge(*linkA->node, 1 - linkA->side, b.edge)) return false;
    if (inUse && (inUse(linkA->node->guid) || inUse(linkB->node->guid))) return false;

    TrackNode seam = MakeNodeAtEnd(graph, *edgeA, a.end);
    seam.links = {TrackNode::Link{a.edge, a.end, 0}, TrackNode::Link{b.edge, b.end, 1}};
    DetachEnd(graph, *edgeA, a.end);
    // a と b が同じノードのとき、先に外した分で添字がずれるので DetachEnd が引き直す。
    DetachEnd(graph, *edgeB, b.end);
    graph.railLinkOverlay.SetLinkNodeOverride({a.edge, a.end}, seam.guid);
    graph.railLinkOverlay.SetLinkNodeOverride({b.edge, b.end}, seam.guid);
    AddOverlayNode(graph, seam);
    return true;
}

bool BranchEdgeEnds(TrackGraph& graph, const EdgeEnd& a, const EdgeEnd& b, const NodeInUseFn& inUse) {
    TrackEdge* edgeA = FindEdge(graph, a.edge);
    TrackEdge* edgeB = FindEdge(graph, b.edge);
    if (edgeA == nullptr || edgeB == nullptr) return false;
    const std::optional<EndLink> linkA = FindEndLink(graph, *edgeA, a.end);
    const std::optional<EndLink> linkB = FindEndLink(graph, *edgeB, b.end);
    if (!linkA || !linkB) return false;
    const int oppositeSide = 1 - linkA->side;
    if (FaceContainsEdge(*linkA->node, oppositeSide, b.edge)) return false;
    if (linkA->node->LinkCountOnSide(oppositeSide) >= 2) return false;
    if (inUse && (inUse(linkA->node->guid) || inUse(linkB->node->guid))) return false;

    const Guid nodeAGuid = linkA->node->guid;
    DetachEnd(graph, *edgeB, b.end);
    TrackNode* nodeA = EnsureEffectiveNode(graph, nodeAGuid);
    nodeA->links.push_back(TrackNode::Link{b.edge, b.end, oppositeSide});
    graph.railLinkOverlay.SetLinkNodeOverride({b.edge, b.end}, nodeAGuid);
    return true;
}

bool DisconnectEdgeEnd(TrackGraph& graph, const EdgeEnd& a, const NodeInUseFn& inUse) {
    TrackEdge* edgeA = FindEdge(graph, a.edge);
    if (edgeA == nullptr) return false;
    const std::optional<EndLink> linkA = FindEndLink(graph, *edgeA, a.end);
    if (!linkA) return false;
    if (linkA->node->LinkCountOnSide(1 - linkA->side) == 0) return false;
    if (inUse && inUse(linkA->node->guid)) return false;

    TrackNode lone = MakeNodeAtEnd(graph, *edgeA, a.end);
    lone.links = {TrackNode::Link{a.edge, a.end, 0}};
    DetachEnd(graph, *edgeA, a.end);
    graph.railLinkOverlay.SetLinkNodeOverride({a.edge, a.end}, lone.guid);
    AddOverlayNode(graph, lone);
    return true;
}

std::optional<EdgeEnd> ResolvePlatformEnd(const TrackGraph& graph, const std::string& stationAddress,
                                          int indexInStation, int end) {
    if (stationAddress.empty() || (end != 0 && end != 1)) return std::nullopt;
    for (const PlatformStopInfo& platform : graph.platforms) {
        if (platform.stationAddress != stationAddress || platform.indexInStation != indexInStation) continue;
        if (platform.railEdges.empty()) return std::nullopt;
        return EdgeEnd{end == 0 ? platform.railEdges.front() : platform.railEdges.back(), end};
    }
    return std::nullopt;
}

}  // namespace rs::world
