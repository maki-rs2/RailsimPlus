//
// ViewportSectionOverlay.h - edge内区間分割(3層)の分割モードUIオーバーレイ
//
// 選択edge上の境界マーカー・選択区間ハイライト・分割位置プレビューの描画と、
// 境界のスクリーンピッキングを担う。ViewportInput.cpp(クリック/ドラッグ処理)と
// ViewportOverlay.cpp(描画呼び出し)の両方から使う。
//
#pragma once

#include <cstddef>
#include <vector>

#include "../../core/Math.h"
#include "../../editor/tool/EditorTypes.h"
#include "../../world/alignment/TrackGraph.h"
#include "../gpu/CameraMath.h"
#include "EditorViewport.h"

namespace rs::app {

// 選択edge上の「区間の境界」1点。indexはIntervalList::Spans()の添字(常に1以上、
// 添字0の区間の開始s=0は境界ではないため含めない)。worldはレール頭頂面まで
// 持ち上げ済みのワールド座標。
struct SectionBoundaryPoint {
    std::size_t index = 0;
    double s = 0.0;
    rs::core::Vec3 world;
};

// 指定edgeの指定層について、境界点を列挙する。edgeが無い/splineが空なら空。
// 描画とピッキングは必ず両方このリストを使うこと(見た目とクリック判定のズレを
// 防ぐため。Editor::NodeRailTopPosと同じ理由)。
std::vector<SectionBoundaryPoint> CollectSectionBoundaries(const rs::world::TrackGraph& graph,
                                                           const rs::world::Guid& edge,
                                                           rs::editor::SectionLayer layer);

// 境界点をスクリーン投影し、(screenX, screenY)から pxRadius 以内で最近傍のものを
// 探す。見つかれば outIndex に SectionBoundaryPoint::index を入れて true。
bool PickSectionBoundary(const std::vector<SectionBoundaryPoint>& points,
                         const rs::app::ViewProjResult& vpr, UINT width, UINT height,
                         float screenX, float screenY, float pxRadius, std::size_t& outIndex);

// 分割モード中の選択edgeについて、境界マーカー・選択区間のハイライト・分割位置
// プレビューを背景ImDrawListへ描く。
void DrawSectionOverlay(const EditorViewportContext& ctx, const rs::app::ViewProjResult& vpr);

} // namespace rs::app
