#include "SingleTrackSection.h"
#include "RailLinkOverlay.h"

#include <algorithm>
#include <deque>
#include <unordered_map>
#include <vector>

#include "BlockRegistry.h"
#include "SpeedSignalRegistry.h"

namespace rs::world {
namespace {

// nodeがちょうど2本のlinkを持ち、それぞれ別の面につながる(分岐でもワープでもない継ぎ目)なら、
// fromEdgeの反対側のedgeを返す。
std::optional<Guid> StraightThroughEdge(const TrackGraph& graph, const Guid& nodeGuid,
                                        const Guid& fromEdge) {
    const TrackNode* node = FindLinkNode(graph, nodeGuid);
    if (node == nullptr || node->links.size() != 2) return std::nullopt;
    const TrackNode::Link& a = node->links[0];
    const TrackNode::Link& b = node->links[1];
    if (a.side == b.side) return std::nullopt;
    if (a.edge == fromEdge) return b.edge;
    if (b.edge == fromEdge) return a.edge;
    return std::nullopt;
}

} // namespace

std::optional<SingleTrackSection> FindSingleTrackSection(
    const TrackGraph& graph, const WarpLinkTable* warpTable, size_t sceneIndex, const Guid& edge,
    std::unordered_set<Guid, GuidHash>* chainEdges) {
    // 閉塞を、分岐しない継ぎ目でつながる範囲まで広げる。
    const std::vector<const Block*> seeds = FindBlocksOnEdge(graph, edge);
    if (seeds.empty()) return std::nullopt;
    std::unordered_set<const Block*> blocks;
    std::deque<const Block*> queue;
    for (const Block* seed : seeds) {
        if (blocks.insert(seed).second) queue.push_back(seed);
    }
    while (!queue.empty()) {
        const Block* block = queue.front();
        queue.pop_front();
        // 閉塞の全edgeについて、そのedgeにかかる閉塞を引いて閉包を取る(どのedgeから始めても同じ区間になる)。
        for (const Guid& blockEdge : block->edges) {
            const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
                graph.edges.find(blockEdge);
            if (edgeIt == graph.edges.end()) continue;
            for (int end = 0; end < 2; ++end) {
                const Guid nodeGuid = LinkNodeOfEnd(graph, edgeIt->second, end);
                const std::optional<Guid> next = StraightThroughEdge(graph, nodeGuid, blockEdge);
                if (!next.has_value()) continue;
                for (const Block* neighbor : FindBlocksOnEdge(graph, *next)) {
                    if (blocks.insert(neighbor).second) queue.push_back(neighbor);
                }
            }
        }
    }

    SingleTrackSection section;
    section.id = (*std::min_element(blocks.begin(), blocks.end(), [](const Block* a, const Block* b) {
                      return a->guid < b->guid;
                  }))->guid;
    std::vector<const SpeedSignal*> signals;
    for (const Block* block : blocks) {
        section.edges.insert(block->edges.begin(), block->edges.end());
        for (const SpeedSignal* signal : SpeedSignalsOfBlock(graph, *block)) {
            if (std::none_of(signals.begin(), signals.end(), [signal](const SpeedSignal* other) {
                    return other->guid == signal->guid;
                })) {
                signals.push_back(signal);
            }
        }
    }
    // 長さは、重なる閉塞や重複したedgeを1回だけ数えて、区間のedge全長を合計する。
    for (const Guid& sectionEdge : section.edges) {
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
            graph.edges.find(sectionEdge);
        if (edgeIt != graph.edges.end()) section.lengthM += EdgeLength(edgeIt->second);
    }
    if (chainEdges != nullptr) chainEdges->insert(section.edges.begin(), section.edges.end());
    if (signals.size() < 2) return std::nullopt;

    // 基準の信号の向きを区間全体へ伝え、逆向きの信号があるかを見る。
    const SpeedSignal* reference = *std::min_element(
        signals.begin(), signals.end(),
        [](const SpeedSignal* a, const SpeedSignal* b) { return a->guid < b->guid; });
    section.masks = MarkTravelDirection(graph, warpTable, sceneIndex,
                                        DirectedEdge{reference->edge, reference->direction > 0},
                                        &section.edges, nullptr);
    const bool hasOpposite = std::any_of(signals.begin(), signals.end(), [&](const SpeedSignal* signal) {
        const TravelDirectionMasks::const_iterator it = section.masks.find(signal->edge);
        const unsigned char bit = signal->direction > 0 ? kTravelAlongBit : kTravelAgainstBit;
        return it != section.masks.end() && (it->second & bit) == 0;
    });
    if (!hasOpposite) return std::nullopt;
    return section;
}

int SectionTravelSlot(const SingleTrackSection& section, const Guid& edge, bool alongEdgeDir) {
    const TravelDirectionMasks::const_iterator it = section.masks.find(edge);
    if (it == section.masks.end()) return 0;
    const unsigned char bit = alongEdgeDir ? kTravelAlongBit : kTravelAgainstBit;
    return (it->second & bit) != 0 ? 0 : 1;
}

} // namespace rs::world
