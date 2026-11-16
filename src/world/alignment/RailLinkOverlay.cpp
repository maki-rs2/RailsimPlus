#include "TrackGraph.h"
#include "RailLinkOverlay.h"

namespace rs::world {

bool RailLinkOverlay::Empty() const {
    return linkNodeOverrides.empty() && effectiveNodes.empty();
}

void RailLinkOverlay::Clear() {
    if (Empty() && originalNodes.empty()) return;
    linkNodeOverrides.clear();
    effectiveNodes.clear();
    originalNodes.clear();
    ++generation;
}

void RailLinkOverlay::SetLinkNodeOverride(const RailLinkEnd& key, const Guid& node) {
    const auto it = linkNodeOverrides.find(key);
    if (it != linkNodeOverrides.end() && it->second == node) return;
    linkNodeOverrides[key] = node;
    ++generation;
}

void RailLinkOverlay::SetEffectiveNode(const Guid& node, const TrackNode& effective,
                                       const TrackNode& original) {
    const auto it = effectiveNodes.find(node);
    const auto sameLinks = [](const std::vector<TrackNode::Link>& a,
                              const std::vector<TrackNode::Link>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].edge != b[i].edge || a[i].end != b[i].end || a[i].side != b[i].side)
                return false;
        return true;
    };
    const auto originalIt = originalNodes.find(node);
    if (it != effectiveNodes.end() && originalIt != originalNodes.end() &&
        it->second.activeSlot == effective.activeSlot &&
        sameLinks(it->second.links, effective.links) &&
        sameLinks(originalIt->second.links, original.links)) return;
    effectiveNodes[node] = effective;
    originalNodes[node] = original;
    ++generation;
}

const TrackNode* FindLinkNode(const TrackGraph& graph, const Guid& node) {
    const auto overlayIt = graph.railLinkOverlay.effectiveNodes.find(node);
    if (overlayIt != graph.railLinkOverlay.effectiveNodes.end()) return &overlayIt->second;
    const auto it = graph.nodes.find(node);
    return it == graph.nodes.end() ? nullptr : &it->second;
}

TrackNode* FindLinkNodeMutable(TrackGraph& graph, const Guid& node) {
    const auto it = graph.railLinkOverlay.effectiveNodes.find(node);
    if (it != graph.railLinkOverlay.effectiveNodes.end()) return &it->second;
    const auto staticIt = graph.nodes.find(node);
    return staticIt == graph.nodes.end() ? nullptr : &staticIt->second;
}

Guid LinkNodeOfEnd(const TrackGraph& graph, const TrackEdge& edge, int end) {
    const auto it = graph.railLinkOverlay.linkNodeOverrides.find(RailLinkEnd{edge.guid, end});
    return it == graph.railLinkOverlay.linkNodeOverrides.end() ? EdgeLinkNode(edge, end) : it->second;
}

TrackNode* EnsureEffectiveNode(TrackGraph& graph, const Guid& node) {
    RailLinkOverlay& overlay = graph.railLinkOverlay;
    const auto it = overlay.effectiveNodes.find(node);
    if (it != overlay.effectiveNodes.end()) return &it->second;
    const auto staticIt = graph.nodes.find(node);
    if (staticIt == graph.nodes.end()) return nullptr;
    overlay.originalNodes[node] = staticIt->second;
    ++overlay.generation;
    return &(overlay.effectiveNodes[node] = staticIt->second);
}

void AddOverlayNode(TrackGraph& graph, const TrackNode& node) {
    graph.railLinkOverlay.effectiveNodes[node.guid] = node;
    graph.railLinkOverlay.originalNodes[node.guid] = TrackNode{};
    ++graph.railLinkOverlay.generation;
}

bool IsRailLinkOverlayStale(const TrackGraph& graph) {
    const RailLinkOverlay& overlay = graph.railLinkOverlay;
    for (const auto& [key, node] : overlay.linkNodeOverrides) {
        if (graph.edges.find(key.edge) == graph.edges.end()) return true;
        if (overlay.effectiveNodes.find(node) == overlay.effectiveNodes.end() &&
            graph.nodes.find(node) == graph.nodes.end()) return true;
    }
    const auto sameLinks = [](const std::vector<TrackNode::Link>& a, const std::vector<TrackNode::Link>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].edge != b[i].edge || a[i].end != b[i].end || a[i].side != b[i].side) return false;
        return true;
    };
    for (const auto& [guid, effective] : overlay.effectiveNodes) {
        for (const TrackNode::Link& link : effective.links)
            if (graph.edges.find(link.edge) == graph.edges.end()) return true;
        const auto originalIt = overlay.originalNodes.find(guid);
        if (originalIt == overlay.originalNodes.end()) continue;
        // 元が空のノードは表にだけある新しいノード。静的側に同じGuidが現れたら食い違い。
        const auto staticIt = graph.nodes.find(guid);
        if (GuidIsNil(originalIt->second.guid)) {
            if (staticIt != graph.nodes.end()) return true;
            continue;
        }
        if (staticIt == graph.nodes.end() || !sameLinks(staticIt->second.links, originalIt->second.links))
            return true;
    }
    return false;
}

bool PruneRailLinkOverlay(TrackGraph& graph) {
    if (!IsRailLinkOverlayStale(graph)) return false;
    graph.railLinkOverlay.Clear();
    return true;
}

} // namespace rs::world
