#include "TrackGraphIntegrity.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "BlockConnectivity.h"
#include "BlockRegistry.h"
#include "GuidJson.h"
#include "PlatformRegistry.h"
#include "SpeedSignalRegistry.h"
#include "TrackTraversal.h"

namespace rs::world {
namespace {

std::string Warning(const Guid& guid, const std::string& detail) {
    return "TrackGraph " + GuidToHex(guid) + ": " + detail;
}

bool IsMisalignedLink(const TrackGraph& graph, const TrackNode& node, const TrackNode::Link& link) {
    const auto edgeIt = graph.edges.find(link.edge);
    if (edgeIt == graph.edges.end() || edgeIt->second.spline.size() < 2) return false;
    if (link.end < 0 || link.end > 1) return false;
    const TrackEdge& edge = edgeIt->second;
    if (edge.linkNodes[static_cast<size_t>(link.end)] == node.guid) return false;
    const rs::core::Vec3& endpoint = link.end == 0 ? edge.spline.front().pos : edge.spline.back().pos;
    const double dx = static_cast<double>(node.pos.x) - endpoint.x;
    const double dy = static_cast<double>(node.pos.y) - endpoint.y;
    const double dz = static_cast<double>(node.pos.z) - endpoint.z;
    return dx * dx + dy * dy + dz * dz > kLinkEndpointToleranceM * kLinkEndpointToleranceM;
}

template <typename Map>
std::vector<Guid> SortedKeys(const Map& map) {
    std::vector<Guid> keys;
    for (const auto& [key, value] : map) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    return keys;
}

template <bool kRepair, typename Map>
void CheckMapGuids(Map& map, TrackGraphGuids& used, std::vector<std::string>& warnings) {
    for (const Guid& key : SortedKeys(map)) {
        const Guid guid = map.at(key).guid;
        if (GuidIsNil(guid) || used.contains(guid)) {
            warnings.push_back(Warning(guid, "Guidが空または重複しています。後続の定義には新しいGuidを割り当てます"));
            if constexpr (kRepair) {
                Guid replacement;
                do { replacement = GenerateGuid(); } while (used.contains(replacement) || map.contains(replacement));
                typename Map::node_type entry = map.extract(key);
                entry.key() = replacement;
                entry.mapped().guid = replacement;
                map.insert(std::move(entry));
                used.insert(replacement);
            }
        } else {
            used.insert(guid);
        }
    }
}

template <bool kRepair, typename Graph>
std::vector<std::string> CheckRepairableIssues(Graph& graph) {
    std::vector<std::string> warnings;
    TrackGraphGuids used;
    CheckMapGuids<kRepair>(graph.nodes, used, warnings);
    CheckMapGuids<kRepair>(graph.edges, used, warnings);
    const auto checkGuid = [&used, &warnings](auto& guid) {
        if constexpr (kRepair) SanitizeTrackGraphGuid(guid, used, warnings);
        else if (GuidIsNil(guid) || !used.insert(guid).second)
            warnings.push_back(Warning(guid, "Guidが空または重複しています"));
    };
    for (auto& station : graph.stations) checkGuid(station.id);
    for (auto& platform : graph.platforms) checkGuid(platform.id);
    for (auto& block : graph.blocks) checkGuid(block.guid);
    for (auto& signal : graph.speedSignals) checkGuid(signal.guid);

    const auto checkEdges = [&graph, &warnings](auto& edges, const Guid& owner, bool physicalOnly) {
        const auto invalid = [&graph, physicalOnly](const Guid& guid) {
            const auto it = graph.edges.find(guid);
            return it == graph.edges.end() || (physicalOnly && IsCenterlineOnlyEdge(it->second));
        };
        for (const Guid& edge : edges) {
            if (invalid(edge)) warnings.push_back(Warning(owner, "使用できないedgeへの参照を除きます: " + GuidToHex(edge)));
        }
        if constexpr (kRepair) std::erase_if(edges, invalid);
    };
    for (auto& [guid, node] : graph.nodes) {
        const std::array<int, 2> oldActiveIndices = {
            node.LinkIndexOnSide(0, node.activeSlot[0]), node.LinkIndexOnSide(1, node.activeSlot[1])};
        std::array<int, 2> newSlots{0, 0}, counts{0, 0};
        size_t index = 0;
        const auto invalidLink = [&graph, &node](const TrackNode::Link& link) {
            return !graph.edges.contains(link.edge) || IsMisalignedLink(graph, node, link);
        };
        for (const TrackNode::Link& link : node.links) {
            if (invalidLink(link)) warnings.push_back(Warning(guid, "消えたedgeまたは端点の位置が合わないlinkを除きます"));
            else if (link.side >= 0 && link.side <= 1) {
                const size_t side = static_cast<size_t>(link.side);
                if (static_cast<int>(index) == oldActiveIndices[side]) newSlots[side] = counts[side];
                ++counts[side];
            }
            ++index;
        }
        for (const int side : {0, 1}) {
            const size_t s = static_cast<size_t>(side);
            const int slot = node.activeSlot[s];
            if (slot < 0 || slot >= std::max(1, node.LinkCountOnSide(side))) {
                warnings.push_back(Warning(guid, "範囲外のactiveSlotを0に戻します"));
            }
            if constexpr (kRepair) node.activeSlot[s] = newSlots[s];
        }
        if constexpr (kRepair) std::erase_if(node.links, invalidLink);
    }
    for (auto& [guid, edge] : graph.edges) {
        for (auto* node : {&edge.nodeA, &edge.nodeB, &edge.linkNodes[0], &edge.linkNodes[1]}) {
            if (GuidIsNil(*node) || graph.nodes.contains(*node)) continue;
            warnings.push_back(Warning(guid, "消えたnodeへの参照を除きます: " + GuidToHex(*node)));
            if constexpr (kRepair) *node = NilGuid();
        }
        checkEdges(edge.trackEdges, guid, false);
    }
    for (auto& platform : graph.platforms) checkEdges(platform.railEdges, platform.id, false);
    for (auto& block : graph.blocks) checkEdges(block.edges, block.guid, true);
    // 空の閉塞は走行で使えず、担当を失った主信号も成立しない。
    if constexpr (kRepair) {
        std::erase_if(graph.blocks, [&warnings](const Block& block) {
            if (!block.edges.empty()) return false;
            warnings.push_back(Warning(block.guid, "範囲のない閉塞を除きます"));
            return true;
        });
        RebuildBlockIndices(graph);
    } else {
        for (const Block& block : graph.blocks)
            if (block.edges.empty()) warnings.push_back(Warning(block.guid, "閉塞の範囲がありません"));
    }
    const auto invalidSignal = [&graph](const SpeedSignal& signal) {
        return !graph.edges.contains(signal.edge) ||
               (rs::world::IsBlockBoundarySignal(signal.kind) && OwnerBlock(graph, signal) == nullptr);
    };
    for (const SpeedSignal& signal : graph.speedSignals) {
        if (invalidSignal(signal)) warnings.push_back(Warning(signal.guid, "edgeまたは担当閉塞を失った信号を除きます"));
    }
    if constexpr (kRepair) std::erase_if(graph.speedSignals, invalidSignal);
    return warnings;
}

} // namespace

void SanitizeTrackGraphGuid(Guid& guid, TrackGraphGuids& used, std::vector<std::string>& warnings) {
    if (!GuidIsNil(guid) && used.insert(guid).second) return;
    const Guid old = guid;
    do { guid = GenerateGuid(); } while (!used.insert(guid).second);
    warnings.push_back(Warning(old, "Guidが空または重複しているため、新しいGuidを割り当てました: " + GuidToHex(guid)));
}

std::vector<std::string> SanitizeTrackGraph(TrackGraph& graph) {
    std::vector<std::string> warnings = CheckRepairableIssues<true>(graph);
    RebuildPlatformIndices(graph);
    RebuildBlockIndices(graph);
    RebuildSpeedSignalIndices(graph);
    PruneRailLinkOverlay(graph);
    return warnings;
}

std::vector<std::string> ValidateTrackGraph(const TrackGraph& graph) {
    std::vector<std::string> warnings = CheckRepairableIssues<false>(graph);
    std::unordered_map<Guid, Guid, GuidHash> memberOwners;
    for (const auto& [guid, node] : graph.nodes) {
        if (guid != node.guid) warnings.push_back(Warning(guid, "nodeのキーとGuidが一致しません"));
        for (const TrackNode::Link& link : node.links) {
            const auto edge = graph.edges.find(link.edge);
            if (link.end < 0 || link.end > 1 || link.side < 0 || link.side > 1) {
                warnings.push_back(Warning(guid, "linkのendまたはsideが範囲外です"));
            } else if (edge != graph.edges.end() && EdgeLinkNode(edge->second, link.end) != guid) {
                warnings.push_back(Warning(guid, "linkとedge端のnodeが一致しません"));
            }
        }
    }
    for (const auto& [guid, edge] : graph.edges) {
        if (guid != edge.guid) warnings.push_back(Warning(guid, "edgeのキーとGuidが一致しません"));
        if (edge.nodeA == edge.nodeB) warnings.push_back(Warning(guid, "edgeが自己ループです"));
        if (EdgeLength(edge) <= 0.0) warnings.push_back(Warning(guid, "edgeの長さが0です"));
        for (const int end : {0, 1}) {
            const Guid& linkNode = EdgeLinkNode(edge, end);
            const auto node = graph.nodes.find(linkNode);
            if (node == graph.nodes.end() || !std::ranges::any_of(node->second.links, [&](const TrackNode::Link& link) {
                    return link.edge == guid && link.end == end;
                })) warnings.push_back(Warning(guid, "edge端に対応するnodeのlinkがありません"));
        }
        if (!edge.centerlineOnly && !edge.trackEdges.empty())
            warnings.push_back(Warning(guid, "実レールが中心線member一覧を持っています"));
        if (!edge.centerlineOnly) continue;
        TrackGraphGuids members;
        for (const Guid& member : edge.trackEdges) {
            const auto it = graph.edges.find(member);
            if (member == guid || !members.insert(member).second || it == graph.edges.end() ||
                it->second.centerlineOnly || it->second.detachedFromCenterline ||
                !memberOwners.try_emplace(member, guid).second)
                warnings.push_back(Warning(guid, "中心線memberの対応が不整合です"));
        }
        // 実レール削除後も中心線の本数・形は凍結するため、member減少は許す。
        if (edge.trackCount < static_cast<int>(members.size()))
            warnings.push_back(Warning(guid, "中心線の本数よりmemberが多くなっています"));
    }
    for (const Block& block : graph.blocks) {
        if (!AnalyzeBlockConnectivity(graph, block).IsConnected())
            warnings.push_back(Warning(block.guid, "閉塞の範囲が連続していません"));
    }
    for (const SpeedSignal& signal : graph.speedSignals) {
        const auto edge = graph.edges.find(signal.edge);
        if (edge != graph.edges.end() && (signal.s < 0.0 || signal.s > EdgeLength(edge->second)))
            warnings.push_back(Warning(signal.guid, "信号の位置sがedgeの範囲外です"));
    }
    return warnings;
}

} // namespace rs::world
