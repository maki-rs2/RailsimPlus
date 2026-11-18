//
// Rs2PlacementNormalize.cpp - Rs2PlacementNormalize.h の実装
//
#include "Rs2PlacementNormalize.h"

#include <unordered_map>
#include <unordered_set>

#include "../../world/scene/MaterializeSnapshot.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {
namespace {

struct PoleAnchor {
    rs::world::Guid edge = rs::world::NilGuid();
    double          s = 0.0;
    double          lateralOffsetM = 0.0;
    double          verticalOffsetM = 0.0;
    rs::world::Guid individualId = rs::world::NilGuid();
};

void AppendIndividuals(const rs::world::Guid& edgeId,
                       const std::vector<rs::world::ImportedPluginLink>& links,
                       std::vector<rs::world::PlacedIndividual>& target,
                       std::unordered_map<std::string, PoleAnchor>* poleAnchors) {
    for (const rs::world::ImportedPluginLink& link : links) {
        rs::world::PlacedIndividual individual;
        individual.edge = edgeId;
        individual.s = link.s;
        individual.direction = +1;
        individual.lateralOffsetM = link.lateralOffsetM;
        individual.verticalOffsetM = link.verticalOffsetM;
        individual.yawOffsetDeg = link.yawOffsetDeg;
        individual.folderUtf8 = link.folderUtf8;
        // モデル無しの架線専用アンカーも、径間端点をGuidで参照できるよう個体化する。
        // 描画列挙側ではfolderUtf8が空の個体を除外する。
        individual.id = rs::world::GenerateGuid();
        target.push_back(individual);
        if (poleAnchors != nullptr && !link.address.empty()) {
            PoleAnchor anchor;
            anchor.edge = edgeId;
            anchor.s = link.s;
            anchor.lateralOffsetM = link.lateralOffsetM;
            anchor.verticalOffsetM = link.verticalOffsetM;
            anchor.individualId = individual.id;
            (*poleAnchors)[link.address] = anchor;
        }
    }
}

} // namespace

rs::world::ManualPlacementOverlay NormalizeImportedPlacements(
    rs::world::TrackGraph& graph, const std::vector<ImportedWireLine>& lines,
    std::vector<std::string>& warnings,
    const std::vector<ImportedWorldAnchoredPole>& worldAnchoredPoles) {
    rs::world::ManualPlacementOverlay overlay;
    std::unordered_map<std::string, PoleAnchor> poleAnchors;

    for (const ImportedWorldAnchoredPole& importedPole : worldAnchoredPoles) {
        if (importedPole.address.empty() || rs::world::GuidIsNil(importedPole.individual.id)) continue;
        overlay.poles.push_back(importedPole.individual);
        PoleAnchor anchor;
        anchor.individualId = importedPole.individual.id;
        poleAnchors[importedPole.address] = anchor;
    }

    for (auto& [edgeId, edge] : graph.edges) {
        AppendIndividuals(edgeId, edge.importedPiers, overlay.piers, nullptr);
        AppendIndividuals(edgeId, edge.importedPoles, overlay.poles, &poleAnchors);
        std::unordered_set<std::string> folders;
        for (const rs::world::ImportedPluginLink& link : edge.importedPiers) {
            if (!link.folderUtf8.empty()) folders.insert(link.folderUtf8);
        }
        for (const rs::world::ImportedPluginLink& link : edge.importedPoles) {
            if (!link.folderUtf8.empty()) folders.insert(link.folderUtf8);
        }
        // 実リンクが1件も無いPier/Pole参照も、.rs2の明示的な「このedgeには無い」を
        // 正しく表現するため、参照folderとして必ず除去対象へ含める。
        if (!edge.importedPierFolderUtf8.empty()) folders.insert(edge.importedPierFolderUtf8);
        if (!edge.importedPoleFolderUtf8.empty()) folders.insert(edge.importedPoleFolderUtf8);
        for (const std::string& folder : folders) {
            rs::world::RemoveFolder(edge.pluginSections, folder);
            // 取り込み配置には自動走査から渡される位相アンカーが存在しない。
            overlay.runAnchors.push_back({edgeId, folder, 0.0, false});
        }

        // 正規化後の再列挙を防ぐ。架線解決に必要なaddressはpoleAnchorsが持つ。
        edge.importedPiers.clear();
        edge.importedPoles.clear();
        edge.importedPierFolderUtf8.clear();
        edge.importedPoleFolderUtf8.clear();
    }

    for (const ImportedWireLine& line : lines) {
        const std::unordered_map<std::string, PoleAnchor>::const_iterator anchorA =
            poleAnchors.find(line.poleAddressA);
        const std::unordered_map<std::string, PoleAnchor>::const_iterator anchorB =
            poleAnchors.find(line.poleAddressB);
        if (anchorA == poleAnchors.end() || anchorB == poleAnchors.end() ||
            rs::world::GuidIsNil(anchorA->second.individualId) ||
            rs::world::GuidIsNil(anchorB->second.individualId)) {
            warnings.push_back("架線径間を破棄: Pole Address解決失敗 (" +
                               line.poleAddressA + " / " + line.poleAddressB + ")");
            continue;
        }
        rs::world::WireSpan span;
        span.id = rs::world::GenerateGuid();
        span.poleA = anchorA->second.individualId;
        span.poleB = anchorB->second.individualId;
        span.branchA = line.branchA;
        span.branchB = line.branchB;
        span.lineFolderUtf8 = line.lineFolderUtf8;
        overlay.lines.push_back(span);
    }
    return overlay;
}

} // namespace rs::app
