#include "Rs2RailBlockImport.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "../../world/alignment/BlockRegistry.h"
#include "Rs2NodeBuild.h"

namespace rs::legacy_layout {

int BuildRailBlocksFromNames(rs::world::TrackGraph& graph, const std::vector<Rs2RailWay>& railWays,
                             const AddressMap& addrMap) {
    std::map<std::string, std::vector<rs::world::Guid>> edgesByName;

    for (const Rs2RailWay& way : railWays) {
        if (way.railBlock.empty()) continue;
        const rs::world::Guid edgeGuid = addrMap.ResolveWay(way.address);
        if (rs::world::GuidIsNil(edgeGuid) || graph.edges.find(edgeGuid) == graph.edges.end()) {
            continue;
        }
        std::vector<rs::world::Guid>& edges = edgesByName[way.railBlock];
        if (std::find(edges.begin(), edges.end(), edgeGuid) == edges.end()) {
            edges.push_back(edgeGuid);
        }
    }

    for (std::pair<const std::string, std::vector<rs::world::Guid>>& entry : edgesByName) {
        std::vector<rs::world::Guid>& edges = entry.second;
        std::sort(edges.begin(), edges.end(),
                  [](const rs::world::Guid& left, const rs::world::Guid& right) {
                      return std::lexicographical_compare(left.begin(), left.end(), right.begin(),
                                                          right.end());
                  });
        rs::world::Block block;
        block.name = entry.first;
        block.edges = std::move(edges);
        rs::world::RegisterBlock(graph, std::move(block));
    }

    return static_cast<int>(edgesByName.size());
}

} // namespace rs::legacy_layout
