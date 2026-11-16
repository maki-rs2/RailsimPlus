//
// PlatformApproach.h - 駅舎(Station)プラグイン向け接近判定
// (`_APPROACH1`/`_APPROACH2`/`_STOPPING`)。
//
// RailSim II は編成側のシーカ走査でコネクタを予約し、駅舎が自分の番線ごとにその状態を
// ビットへまとめる(CStation::SetSwitchStruct)。番線粒度まで落とすと「番線に属する
// edgeのいずれかが[編成の後端 … 先端+制動距離]の区間と重なるか」に等しいので、
// ここではその判定だけを純関数として実装する(GPU/描画非依存、rs::world のみに依存)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "SimTuning.h"
#include "TrainLeadingEnd.h"
#include "../world/alignment/TrackGraph.h"
#include "../world/alignment/TrackTraversal.h"
#include "../world/alignment/WarpForwardScan.h"

namespace rs::sim {

// 接近判定の入力(編成1本ぶん)。呼び出し側(rs::app::SceneTrainController)が
// 走行中の編成から毎tick組み立てる。
struct ApproachTrain {
    // 編成の物理的な先端(いるScene+軌道上位置)。position.directionは進行方向。
    // 順走・逆走を区別せず、ここから進行方向へ制動距離、進行方向の逆へ編成長を走査する。
    LeadingEnd front;
    double speedKmh = 0.0;                // 常に非負(絶対値)
    double maxDecelerationKmhPerS = 0.0;  // 最大減速度(制動距離の算出に使う)
    double trainLengthM = 0.0;            // 編成全長(先端から後方=占有範囲の走査距離)
    // この編成が今「停車中」か(RailSim II の CTrainGroup::m_State==2 = 停止してドアを開けている
    // 状態に相当)。trueなら走査で踏んだ番線に`_STOPPING`ビットも立つ。RailSim II と同じく
    // 停車目標の番線に限らず、走査範囲に重なる番線すべてが対象(停車不可番線も含む)。
    bool stopping = false;
    // 駅の停止目標までの残り距離[m](先端基準)。設定されていれば、前方走査を
    // そこで打ち切る。停止位置の先にある踏切/駅は、この編成が到達しない以上
    // 接近扱いにしない(RailSim II には無い挙動)。停止位置の
    // 上にある番線は距離が一致するので立つ。
    std::optional<double> stopDistanceM;
    // この編成のseek集合による分岐決定(rs::sim::TrainRuntime::GroupSeekDecisions)。
    // 非nullなら、走査中の分岐選択でこの編成が既に決定した経路を共有activeSlotより
    // 優先して使う(先読みと接近判定とで経路の見え方が食い違わないようにするため)。
    const rs::world::NodeSlotOverride* slotOverride = nullptr;
};

// 駅舎1インスタンスぶんの接近マスク。ビット位置は
// rs::world::PlatformStopInfo::indexInStation(駅舎内のPlatform出現順)。
struct StationApproachMask {
    uint32_t approach1 = 0;
    uint32_t approach2 = 0;
    // `_STOPPING`。RailSim II ではレール状態のbit4で、必ずbit1/bit2(=approach1/approach2)と
    // 同時に立つため stopping ⊆ (approach1 | approach2) が不変条件。
    uint32_t stopping = 0;
};

// 全編成ぶんの接近判定を行い、駅プラグイン個体のGuid → マスクの表を返す
// (どの編成も接近していない駅舎はキーごと現れない。呼び出し側は0を書くこと)。
// graphを書き換えない純関数。
std::unordered_map<rs::world::Guid, StationApproachMask, rs::world::GuidHash> ComputePlatformApproach(
    const rs::world::TrackGraph& graph, const std::vector<ApproachTrain>& trains,
    const ApproachTuning& tuning = ApproachTuning{});

// 上のScene跨ぎ版。ワープの先にある番線にも接近ビットを立てる。マスクの書き込み先
// (rs::app::SceneSceneryRuntime)は駅舎のAddressで引くため、非アクティブSceneの駅舎にも
// 届く(Addressはレイアウト全体で一意)。単一Scene版はこれの特殊な場合として実装している。
std::unordered_map<rs::world::Guid, StationApproachMask, rs::world::GuidHash> ComputePlatformApproachAcrossScenes(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, const std::vector<ApproachTrain>& trains,
    const ApproachTuning& tuning = ApproachTuning{});
std::unordered_map<rs::world::Guid, StationApproachMask, rs::world::GuidHash> ComputePlatformApproachAcrossScenes(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, const std::vector<ApproachTrain>& trains,
    const std::vector<rs::world::ForwardScanSpan>& forwardSpans,
    const ApproachTuning& tuning = ApproachTuning{});

// ある編成が「今このtickで前方どこまでを接近判定の対象とするか」の距離[m]。
// RailSim II の seekdist = STOP_LIMIT_LINE + 0.5*v^2/a 相当(CTrainGroup.cpp:33/908)と、
// 定時間ぶんの距離 v*warningTimeS + minDistanceM の大きい方(rs::sim::ApproachTuning)。
// テストから境界を直接検証できるよう公開する。
double ApproachBrakingDistanceM(double speedKmh, double maxDecelerationKmhPerS,
                                const ApproachTuning& tuning = ApproachTuning{});

} // namespace rs::sim
