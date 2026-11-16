//
// WireConnectPoints.h - 架線の接続点(柱が持つ架線の取り付け点)のキャッシュ要素と、
// 実体化済みの柱の接続点位置の計算
//
// 接続点の位置は架線メッシュの始点・終点と同じ点でなければならない。自動配置の径間は
// ViewportRailMeshBuild.cpp:AppendAutoWireSpans が baseA/baseB と同時に積み、実体化済みの
// 柱はここの MaterializedWireBasePos を径間の描画(AppendMaterializedWireSpans)と
// 接続点の列挙(AppendMaterializedConnectPoints)の両方で使う。
//
#pragma once

#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../editor/tool/EditorPlacementOverlayOps.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/scene/ManualPlacementOverlay.h"
#include "../plugin_session/PluginSelector.h"

namespace rs::app {

struct WireConnectPoint {
    rs::editor::WirePointRef ref;
    rs::core::Vec3 pos;
    rs::core::Vec3 up; // 柱の上方向(架線メッシュの向きに使う)
    // この点を1点目にして結ぶときに使う架線プラグイン(その柱のedgeに割り当てられたLine)。
    // 空なら接続に使えない。
    std::string lineFolderUtf8;
};

// 結ばれた接続点のペア1本(径間)。ピッキング(ホバー・選択・切断)用で、branchごとに1件持つ。
struct WireSpanCacheEntry {
    rs::editor::WireSpanRef ref;
    rs::core::Vec3 pos1;
    rs::core::Vec3 pos2;
    std::string lineFolderUtf8;
};

// メッシュ再構築のたびに作り直す、架線編集用のキャッシュ(点と径間)。
struct WireEditCache {
    std::vector<WireConnectPoint> points;
    std::vector<WireSpanCacheEntry> spans;
};

// Pole個体の基準フレームを返す。ワールドアンカーPoleは worldPos + 固定の tangent/up、
// 軌道アンカーPoleは所属edge上の EvaluateAt。edgeが無ければ false。
bool ResolvePoleFrame(const rs::world::TrackGraph& graph, const rs::world::PlacedIndividual& pole,
                      rs::world::TrackFrame& outFrame);

// 実体化済みPoleの接続点(branchOffsetM = WireLateralOffsetsForの該当値)の、架線メッシュの
// 基準点。lineHeightは架線プラグインのHeight(柱の原点からトロリ線までの引き下げ量)。
rs::core::Vec3 MaterializedWireBasePos(const rs::world::TrackFrame& frame,
                                       const rs::world::PlacedIndividual& pole,
                                       float branchOffsetM, float lineHeight);

// edgeに割り当てられているLineプラグイン(最初の1件)。無ければnullptr。
const TrackPluginEntry* FindEdgeLineEntry(const rs::world::TrackGraph& graph,
                                          const PluginSelector& selector,
                                          const rs::world::Guid& edge);

// 実体化済みのPole(軌道上のもの)すべての接続点を out へ積む。径間を持たない柱も含む。
void AppendMaterializedConnectPoints(const rs::world::TrackGraph& graph,
                                     const rs::world::ManualPlacementOverlay& overlay,
                                     const PluginSelector& selector,
                                     std::vector<WireConnectPoint>& out);

// Structの接続点から導出した柱(ワールドアンカー・プラグインなし)の接続点を out へ積む。
// ref.poleId で柱を指し、lineFolderUtf8 は空(架線プラグインは結ぶ相手の点から決める)。
void AppendStructLinePoleConnectPoints(const rs::world::TrackGraph& graph,
                                       const std::vector<rs::world::PlacedIndividual>& poles,
                                       std::vector<WireConnectPoint>& out);

} // namespace rs::app
