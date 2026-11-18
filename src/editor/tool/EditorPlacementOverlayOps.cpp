//
// EditorPlacementOverlayOps.cpp - EditorPlacementOverlayOps.h の実装
//
#include "EditorPlacementOverlayOps.h"

#include "../../core/Epsilon.h"

#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

using rs::world::Guid;

namespace {


std::vector<rs::world::PlacedIndividual>::iterator FindIndividual(
    std::vector<rs::world::PlacedIndividual>& individuals, const Guid& edge, double s,
    const std::string& folderUtf8) {
    // 実体化済みリストは完全リストで、ApplyOverlayToItemsが個体のsをそのまま
    // placementCacheへ出すため、ピッキング結果は個体のsと厳密一致する。
    return std::find_if(individuals.begin(), individuals.end(),
                        [&edge, s, &folderUtf8](const rs::world::PlacedIndividual& individual) {
                            return individual.edge == edge && individual.folderUtf8 == folderUtf8 &&
                                   std::abs(individual.s - s) < rs::core::kPlacementPositionEpsM;
                        });
}

bool HasIndividualForEdgeFolder(const std::vector<rs::world::PlacedIndividual>& individuals,
                                const Guid& edge, const std::string& folderUtf8) {
    return std::any_of(individuals.begin(), individuals.end(),
                       [&edge, &folderUtf8](const rs::world::PlacedIndividual& individual) {
                           return individual.edge == edge && individual.folderUtf8 == folderUtf8;
                       });
}

// 接続点1つの柱のGuidを引く。poleId付きはそのGuid、それ以外は軌道上の柱を引く。
// 柱が無い(未実体化・ワールドアンカー)ときはfalse。
bool FindWirePole(std::vector<rs::world::PlacedIndividual>& poles, const WirePointRef& point,
                  Guid& outPole) {
    if (IsStructLinePoint(point)) {
        outPole = point.poleId;
        return true;
    }
    if (point.edge == rs::world::NilGuid()) return false;
    const std::vector<rs::world::PlacedIndividual>::iterator pole =
        FindIndividual(poles, point.edge, point.s, point.poleFolderUtf8);
    if (pole == poles.end()) return false;
    outPole = pole->id;
    return true;
}

// 2本の柱とその接続点添字を引く。どちらかの柱が無いときはfalse。
bool FindWirePoles(std::vector<rs::world::PlacedIndividual>& poles, const WirePointRef& pointA,
                   const WirePointRef& pointB, Guid& outPoleA, Guid& outPoleB) {
    return FindWirePole(poles, pointA, outPoleA) && FindWirePole(poles, pointB, outPoleB);
}

// 柱ペアの同じ接続点どうしを結ぶ径間を全部消す(向き・架線プラグイン不問)。消した本数を返す。
std::size_t EraseWirePairSpans(std::vector<rs::world::WireSpan>& lines, const Guid& poleA,
                               int branchA, const Guid& poleB, int branchB) {
    const std::size_t before = lines.size();
    lines.erase(std::remove_if(lines.begin(), lines.end(),
                               [&](const rs::world::WireSpan& span) {
                                   return (span.poleA == poleA && span.branchA == branchA &&
                                           span.poleB == poleB && span.branchB == branchB) ||
                                          (span.poleA == poleB && span.branchA == branchB &&
                                           span.poleB == poleA && span.branchB == branchA);
                               }),
                lines.end());
    return before - lines.size();
}

} // namespace

bool IsStructLinePoint(const WirePointRef& point) {
    return !rs::world::GuidIsNil(point.poleId);
}

bool SameWirePoint(const WirePointRef& a, const WirePointRef& b) {
    if (a.poleId != b.poleId) return false;
    if (IsStructLinePoint(a)) return a.branch == b.branch;
    return a.edge == b.edge && a.branch == b.branch && a.poleFolderUtf8 == b.poleFolderUtf8 &&
           std::abs(a.s - b.s) < rs::core::kPlacementPositionEpsM;
}

bool SameWireSpan(const WireSpanRef& x, const WireSpanRef& y) {
    return (SameWirePoint(x.a, y.a) && SameWirePoint(x.b, y.b)) ||
           (SameWirePoint(x.a, y.b) && SameWirePoint(x.b, y.a));
}

bool EditorPlacementOverlayOps::ConnectWirePoints(rs::world::ManualPlacementOverlay& overlay,
                                                  const WirePointRef& pointA,
                                                  const WirePointRef& pointB,
                                                  const std::string& lineFolderUtf8) {
    if (lineFolderUtf8.empty() || SameWirePoint(pointA, pointB)) return false;
    Guid poleA = rs::world::NilGuid();
    Guid poleB = rs::world::NilGuid();
    if (!FindWirePoles(overlay.poles, pointA, pointB, poleA, poleB)) return false;
    EraseWirePairSpans(overlay.lines, poleA, pointA.branch, poleB, pointB.branch);
    overlay.lines.push_back({rs::world::GenerateGuid(), poleA, poleB, pointA.branch,
                             pointB.branch, lineFolderUtf8});
    return true;
}

bool EditorPlacementOverlayOps::DisconnectWirePoints(rs::world::ManualPlacementOverlay& overlay,
                                                     const WirePointRef& pointA,
                                                     const WirePointRef& pointB) {
    Guid poleA = rs::world::NilGuid();
    Guid poleB = rs::world::NilGuid();
    if (!FindWirePoles(overlay.poles, pointA, pointB, poleA, poleB)) return false;
    return EraseWirePairSpans(overlay.lines, poleA, pointA.branch, poleB, pointB.branch) > 0;
}

bool IsNoOpPlacementMove(const Guid& oldEdge, double oldS, float oldLateralOffsetM,
                         const Guid& newEdge, double newS, float newLateralOffsetM) {
    return oldEdge == newEdge && std::abs(newS - oldS) < rs::core::kPlacementPositionEpsM &&
           std::abs(newLateralOffsetM - oldLateralOffsetM) < rs::core::kPlacementLateralEpsM;
}

void EditorPlacementOverlayOps::DeleteSelectedPlacement(rs::world::ManualPlacementOverlay& overlay,
                                                         const Guid& edge, double s,
                                                         const std::string& folderUtf8) {
    std::vector<rs::world::PlacedIndividual>* individualLists[] = {
        &overlay.poles,
        &overlay.piers,
    };
    for (std::vector<rs::world::PlacedIndividual>* individuals : individualLists) {
        const std::vector<rs::world::PlacedIndividual>::iterator individualIt =
            FindIndividual(*individuals, edge, s, folderUtf8);
        if (individualIt == individuals->end()) continue;

        const Guid individualId = individualIt->id;
        individuals->erase(individualIt);
        overlay.lines.erase(std::remove_if(overlay.lines.begin(), overlay.lines.end(),
                                           [&individualId](const rs::world::WireSpan& line) {
                                               return line.poleA == individualId ||
                                                      line.poleB == individualId;
                                           }),
                            overlay.lines.end());
        return;
    }

    spdlog::warn("[PlacementEdit] deletion target was not materialized: s={}, folder={}", s,
                 folderUtf8);
}

bool EditorPlacementOverlayOps::AddManualPlacement(rs::world::ManualPlacementOverlay& overlay,
                                                   const rs::world::TrackGraph& graph,
                                                   const Guid& edge, double s,
                                                   const std::string& folderUtf8,
                                                   bool keepUpright, float verticalOffsetM) {
    if (folderUtf8.empty()) return false;
    if (graph.edges.find(edge) == graph.edges.end()) return false;
    if (!rs::world::EdgeHasFolder(graph.edges.at(edge), folderUtf8)) {
        if (HasIndividualForEdgeFolder(overlay.poles, edge, folderUtf8)) {
            rs::world::PlacedIndividual individual;
            individual.id = rs::world::GenerateGuid();
            individual.edge = edge;
            individual.s = s;
            individual.direction = +1;
            individual.verticalOffsetM = verticalOffsetM;
            individual.folderUtf8 = folderUtf8;
            overlay.poles.push_back(individual);
            return true;
        }
        if (HasIndividualForEdgeFolder(overlay.piers, edge, folderUtf8)) {
            rs::world::PlacedIndividual individual;
            individual.id = rs::world::GenerateGuid();
            individual.edge = edge;
            individual.s = s;
            individual.direction = +1;
            individual.verticalOffsetM = verticalOffsetM;
            individual.folderUtf8 = folderUtf8;
            overlay.piers.push_back(individual);
            return true;
        }
    }
    spdlog::warn("[PlacementEdit] addition target was not materialized: folder={}", folderUtf8);
    return false;
}

bool EditorPlacementOverlayOps::MovePlacementTo(rs::world::ManualPlacementOverlay& overlay,
                                                const rs::world::TrackGraph& graph,
                                                const Guid& oldEdge, double oldS,
                                                const std::string& folderUtf8, const Guid& newEdge,
                                                double newS, bool keepUpright,
                                                float verticalOffsetM, float oldLateralOffsetM,
                                                float newLateralOffsetM,
                                                std::optional<int> newDir) {
    if (graph.edges.find(newEdge) == graph.edges.end()) return false;
    std::vector<rs::world::PlacedIndividual>* individualLists[] = {
        &overlay.poles,
        &overlay.piers,
    };
    for (std::vector<rs::world::PlacedIndividual>* individuals : individualLists) {
        const std::vector<rs::world::PlacedIndividual>::iterator individualIt =
            FindIndividual(*individuals, oldEdge, oldS, folderUtf8);
        if (individualIt == individuals->end()) continue;
        if (oldEdge != newEdge && rs::world::EdgeHasFolder(graph.edges.at(newEdge), folderUtf8)) {
            return false;
        }
        if (IsNoOpPlacementMove(oldEdge, oldS, oldLateralOffsetM, newEdge, newS,
                                newLateralOffsetM))
            return false;

        individualIt->edge = newEdge;
        individualIt->s = newS;
        individualIt->lateralOffsetM = newLateralOffsetM;
        if (newDir.has_value()) individualIt->direction = *newDir;
        // WireSpanはPlacedIndividual::idを参照するため、個体の位置更新だけで架線も追従する。
        return true;
    }
    if (IsNoOpPlacementMove(oldEdge, oldS, oldLateralOffsetM, newEdge, newS, newLateralOffsetM))
        return false;

    spdlog::warn("[PlacementEdit] move target was not materialized: s={}, folder={}", oldS,
                 folderUtf8);
    return false;
}

void EditorPlacementOverlayOps::ReanchorPlacementEditsForEdge(
    rs::world::ManualPlacementOverlay& overlay, const Guid& fromEdge, const Guid& toEdge) {
    for (rs::world::PlacedIndividual& pole : overlay.poles)
        if (pole.edge == fromEdge) pole.edge = toEdge;
    for (rs::world::PlacedIndividual& pier : overlay.piers)
        if (pier.edge == fromEdge) pier.edge = toEdge;
    for (rs::world::RunAnchor& section : overlay.runAnchors)
        if (section.edge == fromEdge) section.edge = toEdge;
}

} // namespace rs::editor
