//
// PlacementSnap.h - 敷設のスナップ候補と優先順位(純関数、GPU/ImGui非依存)
//
// 4種類のスナップ(既存ノード/並走位置/カーブ開始点/スプライン上)のうち、
// 重なったときにどれを採るかを ChooseSnap の1か所で決める。
// 候補の収集のうちグラフだけで決まるもの(並走位置・カーブ境界)もここに置く。
// 画面上のピッキングが要る候補(ノード・スプライン上)は Editor 側で集める。
//
#pragma once

#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackEdge;
struct TrackGraph;
} // namespace rs::world

namespace rs::editor {

// 宣言順がそのまま優先順位(先に書いたものほど強い)。Noneは「候補なし」で最後に置く。
enum class SnapKind { Node, Parallel, CurveStart, OnSpline, None };

// 調整幅[m]。並走位置は線路間隔との差の許容、カーブ開始点は境界までの弧長差の許容。
inline constexpr float kParallelSnapToleranceM = 0.75f;
inline constexpr float kCurveStartSnapRadiusM = 2.0f;

struct PlacementSnap {
    SnapKind kind = SnapKind::None;
    rs::core::Vec3 pos;                              // スナップ後のワールド位置
    float distance = 0.0f;                           // 元の位置からのずれ。同順位の比較に使う
    rs::world::Guid node = rs::world::NilGuid();     // Node: 対象ノード
    rs::world::Guid edge = rs::world::NilGuid();     // Parallel/CurveStart/OnSpline: 基準のedge
    double edgeS = 0.0;                              // 同: edge上の弧長
};

// 候補から1つ選ぶ。種類の順位が先のものを優先し、同じ種類なら distance が小さいもの。
PlacementSnap ChooseSnap(const std::vector<PlacementSnap>& candidates);

// edgeの線形の種類(直線/円弧/緩和曲線)が切り替わる位置の弧長s(両端は含まない)。
std::vector<double> CurveBoundaryS(const rs::world::TrackEdge& edge);

// sに最も近い切替位置が radiusM 以内ならそのsを outS に入れて true。
bool SnapToCurveBoundary(const rs::world::TrackEdge& edge, double s, double radiusM, double& outS);

// 既存レールから spacingM だけ水平に平行にずれた位置のうち、hit に最も近いもの。
// 複線中心線(実体を持たないedge)は基準にしない。その位置にすでに別のレールがあれば候補にしない。
// 見つからなければ false。
bool FindParallelSnap(const rs::world::TrackGraph& graph, const rs::core::Vec3& hit,
                      float spacingM, float toleranceM, PlacementSnap& out);

} // namespace rs::editor
