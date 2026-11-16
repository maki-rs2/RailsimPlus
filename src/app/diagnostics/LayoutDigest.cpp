#include "LayoutDigest.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

#include "../../editor/tool/Editor.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../../world/scene/SceneEntity.h"
#include "../../world/scene/SceneRegistry.h"
#include "../plugin_session/LayoutSession.h"
#include "../plugin_session/PluginSelector.h"
#include "../plugin_session/RslLayoutSerialize.h"

namespace rs::app {
namespace {

struct PlacementCounts {
    size_t pole = 0;
    size_t pier = 0;
    size_t catenary = 0;
    size_t structure = 0;
    size_t stationPlugin = 0;
    size_t runAnchor = 0;
};

using SortedEdges = std::vector<std::pair<rs::world::Guid, const rs::world::TrackEdge*>>;

SortedEdges CollectSortedEdges(const rs::world::TrackGraph& graph) {
    SortedEdges edges;
    edges.reserve(graph.edges.size());
    for (const std::pair<const rs::world::Guid, rs::world::TrackEdge>& pair : graph.edges) {
        edges.emplace_back(pair.first, &pair.second);
    }
    std::sort(edges.begin(), edges.end(),
              [](const SortedEdges::value_type& left, const SortedEdges::value_type& right) {
                  return left.first < right.first;
              });
    return edges;
}

PlacementCounts CountPlacements(const LayoutSceneCache& cache) {
    PlacementCounts counts;
    counts.pole = cache.overlay.poles.size();
    counts.pier = cache.overlay.piers.size();
    counts.catenary = cache.overlay.lines.size() + cache.wireSpans.size();
    counts.runAnchor = cache.overlay.runAnchors.size();
    for (const rs::world::SceneEntity& entity : cache.manualScenery) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery) continue;
        const rs::legacy_pi::PluginType type = DefaultClassifyPluginType(entity.folderUtf8);
        if (type == rs::legacy_pi::PluginType::Struct) ++counts.structure;
        if (type == rs::legacy_pi::PluginType::Station) ++counts.stationPlugin;
    }
    return counts;
}

void AppendSceneDigest(std::ostringstream& output, size_t sceneIndex, const LayoutSceneCache& cache) {
    const rs::world::TrackGraph& graph = cache.graph;
    const std::string prefix = "scene" + std::to_string(sceneIndex) + ".";
    const PlacementCounts placements = CountPlacements(cache);
    std::map<std::pair<int, int>, size_t> linkHistogram;
    for (const std::pair<const rs::world::Guid, rs::world::TrackNode>& pair : graph.nodes) {
        const rs::world::TrackNode& node = pair.second;
        ++linkHistogram[{node.LinkCountOnSide(0), node.LinkCountOnSide(1)}];
    }

    size_t advanced = 0;
    size_t deadEnd = 0;
    double totalLength = 0.0;
    size_t splinePointCount = 0;
    const SortedEdges edges = CollectSortedEdges(graph);
    constexpr double kTraversalPastEndM = 0.001;
    for (const std::pair<rs::world::Guid, const rs::world::TrackEdge*>& item : edges) {
        const rs::world::TrackEdge& edge = *item.second;
        const double edgeLength = rs::world::EdgeLength(edge);
        totalLength += edgeLength;
        splinePointCount += edge.spline.size();
        for (const int direction : {+1, -1}) {
            rs::world::TrackPosition start{item.first, direction > 0 ? 0.0 : edgeLength, direction};
            const rs::world::AdvanceResult traversal =
                rs::world::Advance(graph, start, edgeLength + kTraversalPastEndM);
            if (traversal.visitedEdges.size() > 1) ++advanced;
            else ++deadEnd;
        }
    }

    output << prefix << "nodes=" << graph.nodes.size() << '\n';
    output << prefix << "edges=" << graph.edges.size() << '\n';
    output << prefix << "stations=" << graph.stations.size() << '\n';
    output << prefix << "platforms=" << graph.platforms.size() << '\n';
    output << prefix << "blocks=" << graph.blocks.size() << '\n';
    output << prefix << "speedSignals=" << graph.speedSignals.size() << '\n';
    output << prefix << "pole=" << placements.pole << '\n';
    output << prefix << "pier=" << placements.pier << '\n';
    output << prefix << "catenary=" << placements.catenary << '\n';
    output << prefix << "structure=" << placements.structure << '\n';
    output << prefix << "stationPlugin=" << placements.stationPlugin << '\n';
    output << prefix << "runAnchor=" << placements.runAnchor << '\n';
    output << prefix << "linkHistogram=";
    bool first = true;
    for (const std::pair<const std::pair<int, int>, size_t>& item : linkHistogram) {
        if (!first) output << ' ';
        output << item.first.first << ',' << item.first.second << ':' << item.second;
        first = false;
    }
    output << '\n';
    output << prefix << "traversal=advanced:" << advanced << " deadEnd:" << deadEnd << '\n';
    // 保存はposを0.1mm単位へ丸めるため、往復で末尾桁が揺れる。diffで同一判定したいので
    // 丸め幅より十分粗いcm単位まで落とし、実際の形状の抜けだけが差分に出るようにする。
    output << std::fixed << std::setprecision(2);
    output << prefix << "edgeLength=" << std::round(totalLength * 100.0) / 100.0 << '\n';
    output << prefix << "splinePoints=" << splinePointCount << '\n';
}

LayoutSceneCache BuildActiveSceneCache(const rs::editor::Editor& editor,
                                       const rs::world::SceneRegistry& sceneRegistry,
                                       const CameraController& camera) {
    LayoutSceneCache cache;
    cache.graph = editor.graph;
    cache.overlay = editor.manualOverlay;
    cache.camera = camera.fly;
    cache.hasCamera = true;
    for (const rs::world::SceneEntity& entity : sceneRegistry.Entities()) {
        if (entity.category == rs::world::SceneEntityCategory::Scenery && rs::world::IsUserPlacement(entity)) {
            cache.manualScenery.push_back(entity);
        }
    }
    return cache;
}

}  // namespace

LayoutDigestResult WriteLayoutDigest(const std::string& pathUtf8, const LayoutSession& session,
                                     const rs::editor::Editor& editor,
                                     const rs::world::SceneRegistry& sceneRegistry,
                                     const CameraController& camera,
                                     const PluginSelector& selector) {
    std::vector<LayoutSceneCache> scenes = session.scenes;
    const LayoutSceneCache activeCache = BuildActiveSceneCache(editor, sceneRegistry, camera);
    if (scenes.empty()) scenes.push_back(activeCache);
    else if (session.activeIndex < scenes.size()) scenes[session.activeIndex] = activeCache;

    size_t carCount = 0;
    for (const rs::world::SceneEntity& entity : sceneRegistry.Entities()) {
        if (entity.category == rs::world::SceneEntityCategory::Car && rs::world::IsUserPlacement(entity)) ++carCount;
    }

    std::ostringstream output;
    output << "scenes=" << scenes.size() << '\n';
    output << "formations=" << selector.Formations().size() << '\n';
    output << "cars=" << carCount << '\n';
    output << "warps=" << session.warpLinks.Links().size() << '\n';
    for (size_t sceneIndex = 0; sceneIndex < scenes.size(); ++sceneIndex) {
        AppendSceneDigest(output, sceneIndex, scenes[sceneIndex]);
    }

    const std::filesystem::path path(rs::platform_text::Utf8ToWide(pathUtf8));
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return {false, "Could not open output file."};
    file << output.str();
    if (!file) return {false, "Could not write output file."};
    return {true, {}};
}

}  // namespace rs::app
