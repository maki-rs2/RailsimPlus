//
// PlacementMaterialize.cpp - PlacementMaterialize.h の実装
//
#include "PlacementMaterialize.h"

#include "TrackPluginPlacement.h"
#include "../plugin_session/RailTieHeights.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {
namespace {

const TrackPluginEntry* FindEntry(const std::vector<TrackPluginEntry>& entries,
                                 const std::string& folderUtf8) {
    for (const TrackPluginEntry& entry : entries)
        if (entry.folderUtf8 == folderUtf8) return &entry;
    return nullptr;
}

rs::world::PlacedIndividual MakeIndividual(const TrackPluginPlacementItem& item) {
    rs::world::PlacedIndividual individual;
    individual.id = rs::world::GenerateGuid();
    individual.edge = item.placement.position.edge;
    individual.s = item.placement.position.s;
    individual.direction = item.placement.position.direction;
    individual.lateralOffsetM = item.placement.lateralOffsetM;
    individual.verticalOffsetM = item.placement.verticalOffsetM;
    individual.yawOffsetDeg = item.placement.yawOffsetDeg;
    individual.folderUtf8 = item.folderUtf8;
    return individual;
}
} // namespace

rs::world::MaterializeSnapshot BuildMaterializeSnapshot(
    const rs::world::TrackGraph& graph, const std::vector<TrackPluginEntry>& entries,
    const rs::world::Guid& edge, const std::string& folderUtf8) {
    rs::world::MaterializeSnapshot snapshot;
    const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator edgeIt =
        graph.edges.find(edge);
    if (edgeIt == graph.edges.end() || !rs::world::EdgeHasFolder(edgeIt->second, folderUtf8)) return snapshot;
    snapshot.edge = edge;
    snapshot.folderUtf8 = folderUtf8;
    snapshot.section.edge = edge;
    snapshot.section.folderUtf8 = folderUtf8;

    std::vector<std::string> log;
    std::vector<PierCarryRecord> pierCarryRecords;
    const std::vector<TrackPluginPlacementItem> items =
        CollectTrackPluginPlacements(entries, graph, log, {}, &pierCarryRecords);
    const TrackPluginEntry* entry = FindEntry(entries, folderUtf8);
    if (entry != nullptr && entry->type == rs::legacy_pi::PluginType::Pier) {
        for (const TrackPluginPlacementItem& item : items)
            if (item.folderUtf8 == folderUtf8 && item.placement.position.edge == edge)
                snapshot.piers.push_back(MakeIndividual(item));
        for (const PierCarryRecord& record : pierCarryRecords) {
            if (record.edge != edge || record.folderUtf8 != folderUtf8) continue;
            snapshot.section.anchorS = record.anchorS;
            snapshot.section.hasAnchor = record.hasAnchor;
            break;
        }
    }

    if (entry != nullptr && entry->type == rs::legacy_pi::PluginType::Pole) {
        // edgeのRail+Tie積み上げ量(railTopLift相当)。edge.railTopLiftはメッシュ
        // 再構築時にのみ書き込まれるキャッシュで、実体化(このBuildMaterializeSnapshot)が
        // 先に走る場合の更新される前提が無いため、ActiveFolders+ComputeRailTieHeightsで
        // 都度算出する(TrackPluginPlacement.cppの自動配置と同じ式)。
        const RailTieHeights railTieHeights =
            ComputeRailTieHeights(rs::world::ActiveFolders(edgeIt->second), entries);
        const float railTopLift = railTieHeights.maxRailHeight + railTieHeights.maxTieHeight;
        const std::vector<PoleWalkRun> runs = WalkPoleRuns(entries, graph);
        for (const PoleWalkRun& run : runs) {
            if (run.poleEntry == nullptr || run.poleEntry->folderUtf8 != folderUtf8) continue;
            if (run.lineEntry == nullptr) continue;
            std::vector<rs::world::Guid> ids;
            for (const PoleWalkPoint& point : run.points) {
                if (point.position.edge != edge) continue;
                rs::world::PlacedIndividual pole;
                pole.id = rs::world::GenerateGuid();
                pole.edge = point.position.edge;
                pole.s = point.position.s;
                pole.direction = point.position.direction;
                pole.lateralOffsetM = point.manualLateralOffsetM;
                pole.verticalOffsetM = railTopLift + run.lineEntry->def.line.trolleyAlt +
                                       run.lineEntry->def.line.height;
                pole.folderUtf8 = folderUtf8;
                ids.push_back(pole.id);
                snapshot.poles.push_back(pole);
            }
            const std::vector<float> lateralOffsets = WireLateralOffsetsFor(*run.poleEntry);
            for (std::size_t index = 1; index < ids.size(); ++index) {
                for (std::size_t branch = 0; branch < lateralOffsets.size(); ++branch) {
                    rs::world::WireSpan line;
                    line.id = rs::world::GenerateGuid();
                    line.poleA = ids[index - 1];
                    line.poleB = ids[index];
                    line.branchA = static_cast<int>(branch);
                    line.branchB = static_cast<int>(branch);
                    line.lineFolderUtf8 = run.lineEntry == nullptr ? std::string{} : run.lineEntry->folderUtf8;
                    snapshot.lines.push_back(line);
                }
            }
            for (const PoleRunEdgeSpan& span : run.edgeSpans) {
                if (span.edge != edge) continue;
                bool hasAnchor = false;
                double carry = 0.0;
                for (const PoleWalkPoint& point : run.points) {
                    if (point.absS <= span.cumStart + span.length) {
                        hasAnchor = true;
                        carry = static_cast<double>(point.absS - span.cumStart);
                    }
                }
                snapshot.section.hasAnchor = hasAnchor;
                snapshot.section.anchorS = carry;
                break;
            }
        }
    }
    return snapshot;
}

std::vector<rs::world::MaterializeSnapshot> BuildMaterializeSnapshotsForEdit(
    const rs::world::TrackGraph& graph, const std::vector<TrackPluginEntry>& entries,
    const std::vector<rs::world::Guid>& edges, const std::string& folderUtf8) {
    std::vector<rs::world::MaterializeSnapshot> snapshots;
    snapshots.reserve(edges.size());
    for (const rs::world::Guid& edge : edges) {
        bool alreadyIncluded = false;
        for (const rs::world::MaterializeSnapshot& snapshot : snapshots) {
            if (snapshot.edge == edge) {
                alreadyIncluded = true;
                break;
            }
        }
        if (alreadyIncluded) continue;
        rs::world::MaterializeSnapshot snapshot =
            BuildMaterializeSnapshot(graph, entries, edge, folderUtf8);
        if (!snapshot.Empty()) snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}
} // namespace rs::app
