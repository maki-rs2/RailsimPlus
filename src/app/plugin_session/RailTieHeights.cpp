#include "RailTieHeights.h"

#include "../../plugins/schema/PluginTypeTraits.h"

#include <algorithm>
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {

float ExtrusionOffsetY(rs::legacy_pi::PluginType type, const RailTieHeights& h) {
    const rs::legacy_pi::PluginTypeTraits& traits = rs::legacy_pi::GetPluginTypeTraits(type);
    float base = 0.0f;
    if (traits.profileSupport == rs::legacy_pi::ProfileSupport::Tie) base = h.maxRailHeight;
    else if (traits.multiTrackSource == rs::legacy_pi::MultiTrackSource::Profile) base = h.TopLift();
    return base - h.TopLift();
}

void AccumulateRailTieHeight(RailTieHeights& h, rs::legacy_pi::PluginType type,
                             const rs::legacy_pi::PluginDefinition& def) {
    if (!def.hasRailInfo) return;
    const rs::legacy_pi::ProfileSupport support = rs::legacy_pi::GetPluginTypeTraits(type).profileSupport;
    if (support == rs::legacy_pi::ProfileSupport::Rail)
        h.maxRailHeight = std::max(h.maxRailHeight, def.railHeight);
    else if (support == rs::legacy_pi::ProfileSupport::Tie)
        h.maxTieHeight = std::max(h.maxTieHeight, def.railHeight);
}

RailTieHeights ComputeRailTieHeights(const std::vector<std::string>& trackPlugins,
                                     const std::vector<TrackPluginEntry>& entries) {
    auto hasEntry = [&trackPlugins](const std::string& folderUtf8) {
        return std::find(trackPlugins.begin(), trackPlugins.end(), folderUtf8) !=
               trackPlugins.end();
    };
    RailTieHeights h;
    for (const TrackPluginEntry& entry : entries) {
        if (!hasEntry(entry.folderUtf8)) continue;
        AccumulateRailTieHeight(h, entry.type, entry.def);
    }
    return h;
}

RailTieHeights ComputeRailTieHeights(const std::vector<std::string>& trackPlugins,
                                     const PluginSelector& selector) {
    return ComputeRailTieHeights(trackPlugins, selector.TrackPluginEntries());
}

void UpdateEdgeRailTopLifts(rs::world::TrackGraph& graph,
                            const std::vector<TrackPluginEntry>& entries) {
    for (std::pair<const rs::world::Guid, rs::world::TrackEdge>& entry : graph.edges) {
        rs::world::TrackEdge& edge = entry.second;
        if (edge.spline.size() < 2) continue;
        edge.railTopLift = ComputeRailTieHeights(rs::world::ActiveFolders(edge), entries).TopLift();
    }
}

} // namespace rs::app
