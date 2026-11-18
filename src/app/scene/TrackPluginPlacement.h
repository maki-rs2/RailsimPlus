//
// TrackPluginPlacement.h - レール系プラグインの離散反復配置
//
// Pier(橋脚)とPole(架線柱)を対象とする。Rail/Tie/Girderは敷設エディタによる連続
// 押し出しメッシュのスコープのまま残しSceneEntity化しない。
//
// - Pier: 間隔はPI定義(PierInfo.Interval、TrackPluginEntry::def.pier.interval)を
//   そのまま使うカーブ非依存の単純固定加算(RailSim IIと同じ、
//   CLineBuildCurve.cpp参照)。
// - Pole: 間隔パラメータをPole自身は持たず、同一run上で有効化されているLine
//   (架線)エントリのLineInfo.MaxInterval/MaxDeflectionに従う可変間隔
//   (たわみ量制限アルゴリズム、RailSim IIのCLineBuildCurve::FinishTrace()準拠)。
//   Line側の架線メッシュ本体はEditorViewport.cppのBuildLineWireMesh呼び出し
//   箇所で実装済み。ここではPole配置点列(WalkPoleRuns)の算出のみを行う。
//
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/TrackPosition.h"
#include "../../world/scene/ManualPlacementOverlay.h"
#include "../../world/scene/RunAnchor.h"
#include "../../world/scene/ScenePlacement.h"
#include "../plugin_session/PluginSelector.h"
#include "TrackPluginRunWalk.h"
#include "../../world/alignment/Guid.h"
#include "../../plugins/schema/PluginHeader.h"

namespace rs::legacy_pi {
struct PluginDefinition;
} // namespace rs::legacy_pi

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::app {

// typeがPierかどうか(固定間隔の反復配置対象か)。Poleは間隔をLine側から得る
// 別経路(WalkPoleRuns/CollectTrackPluginPlacements内)で処理するためここには
// 含まない。
bool IsDiscreteRailPlacementType(rs::legacy_pi::PluginType type);

// エントリの複線一括本数(RailSim II の TrackNum)。格納先は種別ごとに異なる(Pier=def.pier、
// Girder=def直下、Pole=def.pole、それ以外は1固定)。1=単線用(実レールに配置)、
// >1=複線一括(中心線に配置。束ねる本数は問わない)。手動追加/移動時のretarget判定(ResolveCenterlineEdgeFor)にも同じ規約で使う。
int MultiTrackNum(const rs::legacy_pi::PluginDefinition& def, rs::legacy_pi::PluginType type);

// エントリの並走軌道間隔[m](TrackInterval)。格納先はMultiTrackNumと同じ。それ以外の種別は0。
// 形や位置の計算には使わない。敷設設定との食い違いの警告(ConfirmMultiTrack)と説明文にだけ使う。
float MultiTrackInterval(const rs::legacy_pi::PluginDefinition& def, rs::legacy_pi::PluginType type);

// 敷設設定(本数・線間隔)とエントリの複線仕様の食い違い。
enum class MultiTrackMismatch { None, TrackNum, TrackInterval };

// 本数が違えばTrackNum。本数が同じで2本以上のとき、線間隔が違えばTrackInterval。
// 線間隔はfloatの完全一致で比べる(RailSim II と同じ)。
MultiTrackMismatch ConfirmMultiTrack(const rs::legacy_pi::PluginDefinition& def,
                                     rs::legacy_pi::PluginType type, int trackNum,
                                     float trackInterval);

// 複線一括仕様(trackNum>1)のPier/Poleを手動追加/移動する際、クリックされたedge
// (pickedEdge)が実レールのままだと自動配置と同じ中心線routing(centerlineOnly edge。
// 束ねる本数は問わない)に乗らない(CenterlineChainsForと同じ規約)。
// pickedEdge自体が中心線ならそのまま返し、実レールなら「trackEdgesにpickedEdgeを
// 含む」中心線edgeのguidを探して返す。見つからなければ
// NilGuid(単線プラグイン、または対応する中心線が無いレイアウト)。
// trackNum<=1(単線)を渡すのは呼び出し側の誤りだが、この関数はNilGuidを返すのみで
// 安全側に倒す(単線はrouting不要)。
rs::world::Guid ResolveCenterlineEdgeFor(const rs::world::TrackGraph& graph,
                                         const rs::world::Guid& pickedEdge, int trackNum);

// 複線一括Pole(TrackNum>1)の架線接続点の、Pole基準位置からの左右オフセット列[m]
// (right = cross(up, dir)方向、右が正)。PoleInfo.TrackInterval(実PIの例:
// TrackNum=2; TrackInterval=4.0)で軌道間隔を指定するPoleは、モデル自体は
// 中心線1本に配置するが架線接続点はTrackNum本ぶん必要(1本のPoleが上下線等
// 複数の架線を支持するため)。オフセットは中心を0としてtrackInterval間隔で
// 対称に並べる(TrackNum=2なら[-interval/2, +interval/2]、3なら
// [-interval, 0, +interval])。TrackNum<=1またはTrackInterval<=0なら
// 単一{0.0f}(オフセット無し、中心線上1本)を返す。
std::vector<float> WireLateralOffsetsFor(const TrackPluginEntry& poleEntry);

// CollectTrackPluginPlacements の1件分(配置先プラグインフォルダと配置情報)。
struct TrackPluginPlacementItem {
    std::string folderUtf8;
    rs::world::TrackPlacement placement;
    // 軌道に載らない柱(ワールド座標へ直接アンカーした個体)の配置。値があるときは
    // placementを使わず、この位置・向きで描く。
    std::optional<rs::world::FreePlacement> worldPlacement;
};

// 項目のワールド行列。worldPlacementがあればそれ、無ければplacementで解決する。
rs::core::Mat4 ResolvePlacementItemWorldMatrix(const TrackPluginPlacementItem& item,
                                               const rs::world::TrackGraph& graph);

// Pier のrun走査中に得られる、edge始点基準の走査方向位相アンカー。
struct PierCarryRecord {
    rs::world::Guid edge;
    std::string folderUtf8;
    double anchorS = 0.0;
    bool hasAnchor = false;
};

// entries内のIsDiscreteRailPlacementType(entry.type)な各エントリを、
// TrackChain::BuildEdgeChainsで求めたチェーン単位で、entry.def.pier.interval
// (<=0なら既定20.0mへフォールバック)間隔で配置し直す。
// エントリはedge独立(TrackEdge::trackPlugins)であり、チェーンを
// 「そのentryが有効なedgeだけの連続区間(run)」へ分割し、run単位でs=0から間隔を
// 敷き直す(runをまたいで位相はリセットされる。同一entryが有効なedgeが途切れなく
// 連続する限りは間隔も連続する)。Pole(架線柱)はWalkPoleRuns経由で同様に走査する。
// 純粋な配置リストのみを返す(SceneRegistry非依存。呼び出し側がGPUインスタンス
// バッチ等、用途に応じて反映する。副作用を持たせないのは、
// SceneRegistry Upsert方式ではなくInstanceBatchManager方式で反映するため)。
std::vector<TrackPluginPlacementItem> CollectTrackPluginPlacements(
    const std::vector<TrackPluginEntry>& entries, const rs::world::TrackGraph& graph,
    std::vector<std::string>& log,
    const std::vector<rs::world::RunAnchor>& runAnchors = {},
    std::vector<PierCarryRecord>* pierCarryRecords = nullptr);

} // namespace rs::app
