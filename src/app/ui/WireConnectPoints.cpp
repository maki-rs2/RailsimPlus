#include "WireConnectPoints.h"

#include <cmath>
#include <numbers>
#include <unordered_map>

#include "../../world/alignment/Alignment.h"
#include "../../world/scene/ScenePlacement.h"
#include "../scene/TrackPluginPlacement.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {

bool ResolvePoleFrame(const rs::world::TrackGraph& graph, const rs::world::PlacedIndividual& pole,
                      rs::world::TrackFrame& outFrame) {
    if (pole.hasWorldAnchor) {
        outFrame.pos = pole.worldPos;
        const float yawRad = pole.worldYawDeg * std::numbers::pi_v<float> / 180.0f;
        outFrame.tangent = rs::core::Vec3(std::sin(yawRad), 0.0f, std::cos(yawRad));
        outFrame.up = rs::core::Vec3(0.0f, 1.0f, 0.0f);
        return true;
    }
    const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator edge =
        graph.edges.find(pole.edge);
    if (edge == graph.edges.end()) return false;
    outFrame = rs::world::EvaluateAt(edge->second, pole.s, pole.direction);
    return true;
}

rs::core::Vec3 MaterializedWireBasePos(const rs::world::TrackFrame& frame,
                                       const rs::world::PlacedIndividual& pole,
                                       float branchOffsetM, float lineHeight) {
    // 接続点の並びは柱自身の向き基準の右方向(常に鉛直に立つ柱の姿勢。ResolveWorldMatrixの
    // keepUprightと同じ)。軌道と逆向きに置かれた柱では左右が入れ替わる。ワールドアンカーPoleは
    // frame.tangentが個体の向きそのもの。
    const float poleYawDeg = pole.hasWorldAnchor ? 0.0f : static_cast<float>(pole.yawOffsetDeg);
    const rs::core::Vec3 poleRight = rs::world::ResolveUprightFrame(frame.tangent, poleYawDeg).right;
    // ワールドアンカーPoleは軌道フレームの横オフセットを持たない(worldPosが個体そのものの位置)が、
    // トロリ線高さ(line.height分の引き下げ)と枝ごとの横オフセットは軌道アンカーPoleと同じ。
    if (pole.hasWorldAnchor) return frame.pos + poleRight * branchOffsetM - frame.up * lineHeight;
    // 柱の位置は軌道の右方向で測った横オフセット(ResolveWorldMatrixと同じ)。
    const rs::core::Vec3 trackRight = rs::core::Vec3::Cross(frame.up, frame.tangent).Normalized();
    return frame.pos + trackRight * static_cast<float>(pole.lateralOffsetM) +
           poleRight * branchOffsetM +
           frame.up * (static_cast<float>(pole.verticalOffsetM) - lineHeight);
}

const TrackPluginEntry* FindEdgeLineEntry(const rs::world::TrackGraph& graph,
                                          const PluginSelector& selector,
                                          const rs::world::Guid& edge) {
    const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator edgeIt =
        graph.edges.find(edge);
    if (edgeIt == graph.edges.end()) return nullptr;
    for (const std::string& folder : rs::world::ActiveFolders(edgeIt->second)) {
        const TrackPluginEntry* entry = selector.FindTrackPluginEntry(folder);
        if (entry != nullptr && entry->type == rs::legacy_pi::PluginType::Line &&
            entry->def.line.hasLineInfo)
            return entry;
    }
    return nullptr;
}

void AppendMaterializedConnectPoints(const rs::world::TrackGraph& graph,
                                     const rs::world::ManualPlacementOverlay& overlay,
                                     const PluginSelector& selector,
                                     std::vector<WireConnectPoint>& out) {
    for (const rs::world::PlacedIndividual& pole : overlay.poles) {
        // ワールドアンカーPoleと架線専用アンカー(folder無し)は対象外。
        if (pole.hasWorldAnchor || pole.folderUtf8.empty() ||
            pole.edge == rs::world::NilGuid())
            continue;
        const TrackPluginEntry* lineEntry = FindEdgeLineEntry(graph, selector, pole.edge);
        if (lineEntry == nullptr) continue;
        rs::world::TrackFrame frame;
        if (!ResolvePoleFrame(graph, pole, frame)) continue;
        const TrackPluginEntry* poleEntry = selector.FindTrackPluginEntry(pole.folderUtf8);
        const std::vector<float> offsets =
            poleEntry == nullptr ? std::vector<float>{0.0f} : WireLateralOffsetsFor(*poleEntry);
        for (std::size_t branch = 0; branch < offsets.size(); ++branch) {
            WireConnectPoint point;
            point.ref = {pole.edge, pole.s, pole.folderUtf8, static_cast<int>(branch)};
            point.pos = MaterializedWireBasePos(frame, pole, offsets[branch],
                                            lineEntry->def.line.height);
            point.up = frame.up;
            point.lineFolderUtf8 = lineEntry->folderUtf8;
            out.push_back(std::move(point));
        }
    }
}

void AppendStructLinePoleConnectPoints(const rs::world::TrackGraph& graph,
                                       const std::vector<rs::world::PlacedIndividual>& poles,
                                       std::vector<WireConnectPoint>& out) {
    for (const rs::world::PlacedIndividual& pole : poles) {
        rs::world::TrackFrame frame;
        if (!ResolvePoleFrame(graph, pole, frame)) continue;
        WireConnectPoint point;
        point.ref.poleId = pole.id;
        // 導出した柱の接続点は1つ。架線プラグイン(=引き下げる高さ)は結ぶ相手で決まるため、
        // 印は支持点そのものに置く。
        point.ref.branch = 0;
        point.pos = frame.pos;
        point.up = frame.up;
        out.push_back(std::move(point));
    }
}

} // namespace rs::app
