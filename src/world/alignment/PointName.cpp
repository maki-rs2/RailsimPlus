#include "PointName.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "NumberedName.h"
#include "TrackGraph.h"

namespace rs::world {
namespace {

constexpr const char* kPointNamePrefix = "分岐 ";

std::unordered_set<std::string> UsedPointNames(const TrackGraph& graph) {
    std::unordered_set<std::string> used;
    for (const auto& [guid, node] : graph.nodes) {
        if (!node.pointName.empty()) used.insert(node.pointName);
    }
    return used;
}

}  // namespace

bool AssignDefaultPointName(TrackGraph& graph, const Guid& node) {
    const auto it = graph.nodes.find(node);
    if (it == graph.nodes.end() || !it->second.IsPoint() || !it->second.pointName.empty()) {
        return false;
    }
    it->second.pointName = NextNumberedName(kPointNamePrefix, UsedPointNames(graph));
    return true;
}

int AssignDefaultPointNames(TrackGraph& graph) {
    std::vector<Guid> unnamed;
    for (const auto& [guid, node] : graph.nodes) {
        if (node.IsPoint() && node.pointName.empty()) unnamed.push_back(guid);
    }
    std::sort(unnamed.begin(), unnamed.end());
    // 使用済みの集合を1回だけ作り、振るたびに足す(毎回グラフ全体を走査しない)。
    std::unordered_set<std::string> used = UsedPointNames(graph);
    for (const Guid& guid : unnamed) {
        std::string name = NextNumberedName(kPointNamePrefix, used);
        used.insert(name);
        graph.nodes.at(guid).pointName = std::move(name);
    }
    return static_cast<int>(unnamed.size());
}

}  // namespace rs::world
