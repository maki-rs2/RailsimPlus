//
// ManualPlacementOverlayApply.cpp - ManualPlacementOverlayApply.h の実装
//
#include "ManualPlacementOverlayApply.h"

#include <algorithm>
#include <cmath>

namespace rs::app {

std::vector<TrackPluginPlacementItem> ApplyOverlayToItems(
    const std::vector<TrackPluginPlacementItem>& items,
    const rs::world::ManualPlacementOverlay& overlay,
    const std::vector<TrackPluginEntry>& entries) {
    if (overlay.Empty()) return items;

    std::vector<TrackPluginPlacementItem> out;
    out.reserve(items.size() + overlay.poles.size() + overlay.piers.size());
    out.insert(out.end(), items.begin(), items.end());
    const auto appendIndividuals = [&out, &entries](const std::vector<rs::world::PlacedIndividual>& individuals) {
        for (const rs::world::PlacedIndividual& individual : individuals) {
            if (individual.folderUtf8.empty()) continue;
            // ワールドアンカー個体(軌道に載っていないPole)はedge/s前提の
            // TrackPlacementで表現できない。ここへ流すとResolveWorldMatrixが
            // edge未解決で単位行列を返し、原点にPoleモデルが浮いて見える
            // (このPole自体のモデル描画は行わない。架線径間の追従・
            // 保存だけを対象にする)。
            if (individual.hasWorldAnchor) continue;
            const std::vector<TrackPluginEntry>::const_iterator entryIt = std::find_if(
                entries.begin(), entries.end(), [&individual](const TrackPluginEntry& entry) {
                return entry.folderUtf8 == individual.folderUtf8;
            });
            if (entryIt == entries.end() && !entries.empty()) continue;
            TrackPluginPlacementItem item;
            item.folderUtf8 = individual.folderUtf8;
            item.placement.position.edge = individual.edge;
            item.placement.position.s = individual.s;
            item.placement.position.direction = individual.direction;
            item.placement.lateralOffsetM = static_cast<float>(individual.lateralOffsetM);
            item.placement.verticalOffsetM = static_cast<float>(individual.verticalOffsetM);
            item.placement.yawOffsetDeg = static_cast<float>(individual.yawOffsetDeg);
            // Pole/Pierは現実に傾かないため常にkeepUpright(entriesに一致が無い
            // .rs2取り込み由来の個体も含む。entryItはこの少し前のフォルダ一致フィルタ
            // でのみ使い、keepUprightの判定材料には使わない)。
            item.placement.keepUpright = true;
            out.push_back(item);
        }
    };
    appendIndividuals(overlay.poles);
    appendIndividuals(overlay.piers);
    return out;
}

int SanitizeOverlay(rs::world::ManualPlacementOverlay& overlay,
                    rs::world::TrackGraph& graph,
                    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& derivedPoleIds) {
    int removedRefs = 0;
    const size_t beforePoles = overlay.poles.size();
    const size_t beforePiers = overlay.piers.size();
    const size_t beforeLines = overlay.lines.size();
    const size_t beforeSections = overlay.runAnchors.size();

    auto edgeExists = [&graph](const rs::world::Guid& e) {
        return graph.edges.find(e) != graph.edges.end();
    };
    // ワールドアンカー個体(軌道に載っていないPole)はedgeを持たない(NilGuidの
    // まま)ことが正常なので、edge存在チェックの対象から除外する。除外しないと
    // 保存→再読込のたびにこの個体と、それを参照するWireSpanが消えてしまう。
    auto isDanglingTrackAnchored = [&edgeExists](const rs::world::PlacedIndividual& item) {
        return !item.hasWorldAnchor && !edgeExists(item.edge);
    };

    overlay.poles.erase(std::remove_if(overlay.poles.begin(), overlay.poles.end(),
        isDanglingTrackAnchored), overlay.poles.end());
    overlay.piers.erase(std::remove_if(overlay.piers.begin(), overlay.piers.end(),
        isDanglingTrackAnchored), overlay.piers.end());
    overlay.runAnchors.erase(std::remove_if(overlay.runAnchors.begin(), overlay.runAnchors.end(),
        [&](const rs::world::RunAnchor& item) { return !edgeExists(item.edge); }), overlay.runAnchors.end());
    overlay.lines.erase(std::remove_if(overlay.lines.begin(), overlay.lines.end(), [&](const rs::world::WireSpan& line) {
        const auto hasPole = [&](const rs::world::Guid& id) {
            if (derivedPoleIds.find(id) != derivedPoleIds.end()) return true;
            return std::any_of(overlay.poles.begin(), overlay.poles.end(), [&](const rs::world::PlacedIndividual& pole) { return pole.id == id; });
        };
        return !hasPole(line.poleA) || !hasPole(line.poleB);
    }), overlay.lines.end());
    struct MaterializedFolder {
        rs::world::Guid edge;
        std::string folderUtf8;
    };
    std::vector<MaterializedFolder> foldersToRemove;
    const auto appendMaterializedFolder = [&foldersToRemove](
        const rs::world::PlacedIndividual& item) {
        const std::vector<MaterializedFolder>::const_iterator existing = std::find_if(
            foldersToRemove.begin(), foldersToRemove.end(), [&item](
                const MaterializedFolder& folder) {
                return folder.edge == item.edge && folder.folderUtf8 == item.folderUtf8;
            });
        if (existing == foldersToRemove.end()) {
            foldersToRemove.push_back({item.edge, item.folderUtf8});
        }
    };
    for (const rs::world::PlacedIndividual& item : overlay.poles)
        appendMaterializedFolder(item);
    for (const rs::world::PlacedIndividual& item : overlay.piers)
        appendMaterializedFolder(item);

    for (const MaterializedFolder& folder : foldersToRemove) {
        std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::iterator edgeIt =
            graph.edges.find(folder.edge);
        if (edgeIt == graph.edges.end() ||
            !rs::world::EdgeHasFolder(edgeIt->second, folder.folderUtf8)) {
            continue;
        }
        rs::world::TrackEdge& edge = edgeIt->second;
        for (std::size_t index = 0; index < edge.pluginSections.SpanCount(); ++index) {
            rs::world::PluginSet value = edge.pluginSections.Spans()[index].value;
            value.plugins.erase(std::remove(value.plugins.begin(), value.plugins.end(),
                                            folder.folderUtf8), value.plugins.end());
            edge.pluginSections.SetSpanValue(index, value);
        }
        // 呼び出し元にログ出力先がなく、純関数の約束事を維持するためここでは件数で通知する。
        ++removedRefs;
    }

    removedRefs += static_cast<int>(beforePoles - overlay.poles.size());
    removedRefs += static_cast<int>(beforePiers - overlay.piers.size());
    removedRefs += static_cast<int>(beforeLines - overlay.lines.size());
    removedRefs += static_cast<int>(beforeSections - overlay.runAnchors.size());
    return removedRefs;
}

} // namespace rs::app
