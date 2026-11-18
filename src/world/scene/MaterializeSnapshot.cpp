//
// MaterializeSnapshot.cpp - MaterializeSnapshot.h の実装
//
#include "MaterializeSnapshot.h"

#include <algorithm>
#include <cmath>
#include "../alignment/TrackGraph.h"

namespace rs::world {
namespace {

template <typename T>
void EraseSnapshotItems(std::vector<T>& target, const std::vector<T>& snapshot) {
    target.erase(std::remove_if(target.begin(), target.end(), [&snapshot](const T& item) {
        return std::any_of(snapshot.begin(), snapshot.end(), [&item](const T& candidate) {
            return candidate.id == item.id;
        });
    }), target.end());
}
} // namespace

void RemoveFolder(IntervalList<PluginSet>& runAnchors, const std::string& folderUtf8) {
    for (std::size_t index = 0; index < runAnchors.SpanCount(); ++index) {
        PluginSet value = runAnchors.Spans()[index].value;
        value.plugins.erase(std::remove(value.plugins.begin(), value.plugins.end(), folderUtf8),
                            value.plugins.end());
        runAnchors.SetSpanValue(index, value);
    }
}

bool MaterializeSnapshot::Empty() const {
    return GuidIsNil(edge) || folderUtf8.empty();
}

bool ApplyMaterializeSnapshot(TrackGraph& graph, ManualPlacementOverlay& overlay,
                              const MaterializeSnapshot& snapshot,
                              IntervalList<PluginSet>& savedSections) {
    if (snapshot.Empty()) return false;
    const auto edgeIt = graph.edges.find(snapshot.edge);
    if (edgeIt == graph.edges.end() || !EdgeHasFolder(edgeIt->second, snapshot.folderUtf8)) return false;
    savedSections = edgeIt->second.pluginSections;
    overlay.poles.insert(overlay.poles.end(), snapshot.poles.begin(), snapshot.poles.end());
    overlay.piers.insert(overlay.piers.end(), snapshot.piers.begin(), snapshot.piers.end());
    overlay.lines.insert(overlay.lines.end(), snapshot.lines.begin(), snapshot.lines.end());
    overlay.runAnchors.push_back(snapshot.section);
    RemoveFolder(edgeIt->second.pluginSections, snapshot.folderUtf8);
    return true;
}

void RevertMaterializeSnapshot(TrackGraph& graph, ManualPlacementOverlay& overlay,
                               const MaterializeSnapshot& snapshot,
                               const IntervalList<PluginSet>& savedSections) {
    const auto edgeIt = graph.edges.find(snapshot.edge);
    if (edgeIt != graph.edges.end()) edgeIt->second.pluginSections = savedSections;
    EraseSnapshotItems(overlay.poles, snapshot.poles);
    EraseSnapshotItems(overlay.piers, snapshot.piers);
    EraseSnapshotItems(overlay.lines, snapshot.lines);
    overlay.runAnchors.erase(std::remove_if(overlay.runAnchors.begin(), overlay.runAnchors.end(),
        [&snapshot](const RunAnchor& section) {
            return section.edge == snapshot.edge && section.folderUtf8 == snapshot.folderUtf8;
        }), overlay.runAnchors.end());
}

void DematerializeEdgeFolder(TrackGraph& graph, ManualPlacementOverlay& overlay,
                             const Guid& edge, const std::string& folderUtf8) {
    const auto edgeIt = graph.edges.find(edge);
    if (edgeIt == graph.edges.end()) return;
    for (std::size_t index = 0; index < edgeIt->second.pluginSections.SpanCount(); ++index) {
        PluginSet value = edgeIt->second.pluginSections.Spans()[index].value;
        if (std::find(value.plugins.begin(), value.plugins.end(), folderUtf8) ==
            value.plugins.end()) {
            value.plugins.push_back(folderUtf8);
            edgeIt->second.pluginSections.SetSpanValue(index, value);
        }
    }
    std::vector<Guid> poleIds;
    for (const PlacedIndividual& pole : overlay.poles)
        if (pole.edge == edge && pole.folderUtf8 == folderUtf8) poleIds.push_back(pole.id);
    overlay.poles.erase(std::remove_if(overlay.poles.begin(), overlay.poles.end(),
        [&edge, &folderUtf8](const PlacedIndividual& item) {
            return item.edge == edge && item.folderUtf8 == folderUtf8;
        }), overlay.poles.end());
    overlay.piers.erase(std::remove_if(overlay.piers.begin(), overlay.piers.end(),
        [&edge, &folderUtf8](const PlacedIndividual& item) {
            return item.edge == edge && item.folderUtf8 == folderUtf8;
        }), overlay.piers.end());
    overlay.lines.erase(std::remove_if(overlay.lines.begin(), overlay.lines.end(),
        [&poleIds](const WireSpan& line) {
            return std::find(poleIds.begin(), poleIds.end(), line.poleA) != poleIds.end() ||
                   std::find(poleIds.begin(), poleIds.end(), line.poleB) != poleIds.end();
        }), overlay.lines.end());
    overlay.runAnchors.erase(std::remove_if(overlay.runAnchors.begin(), overlay.runAnchors.end(),
        [&edge, &folderUtf8](const RunAnchor& section) {
            return section.edge == edge && section.folderUtf8 == folderUtf8;
        }), overlay.runAnchors.end());
}

void RescaleMaterializedEdgeItems(ManualPlacementOverlay& overlay, const Guid& edge,
                                  double oldLen, double newLen) {
    if (oldLen <= 0.0 || newLen <= 0.0 || std::abs(newLen - oldLen) < kMinSpanLen) return;
    const double ratio = newLen / oldLen;
    for (PlacedIndividual& item : overlay.poles) if (item.edge == edge) item.s *= ratio;
    for (PlacedIndividual& item : overlay.piers) if (item.edge == edge) item.s *= ratio;
    for (RunAnchor& section : overlay.runAnchors)
        if (section.edge == edge) section.anchorS *= ratio;
}
} // namespace rs::world
