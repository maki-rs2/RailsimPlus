#include "PlatformApproach.h"
#include "../core/SpeedUnits.h"
#include "SimTuning.h"

#include <algorithm>
#include <cmath>

#include "TrainKinematics.h"
#include "../world/alignment/TrackForwardScan.h"
#include "../world/alignment/PlatformRegistry.h"
#include "../world/alignment/TrackGraph.h"

namespace rs::sim {

namespace {


// RailSim II の STOP_LIMIT_LINE(停止余裕距離)相当。制動距離にこの余裕を足したものが
// 接近判定の前方範囲になる。

// 走行方向とedgeの向きの、どちらの組み合わせを`_APPROACH1`に割り当てるか。
// false = 走行方向がedgeのnodeA→nodeB向きと**逆**の側が`_APPROACH1`。
//
// RailSim II 側はedgeではなくコネクタの面(m_Side)基準で分けており、どちらの物理的向きが
// `_APPROACH1`になるかはデータからは決められないので、実機の挙動に合わせて
// falseにしている。向きの判定はこの1行に閉じている。他の場所に向き判定を書き足さないこと。
constexpr bool kApproach1IsAlongEdgeDir = false;

// 32bitマスクなので、これを超えるビット位置の番線は表現できない(RailSim II も同じ制限)。
constexpr int kMaxPlatformBits = 32;

// maxDistanceMは、この走査で接近扱いにする上限[m]。共有spanは他の使う側に合わせた
// 長い距離まで走査されているので、ここで自分の上限へ切らないと接近ビットが
// 先読み距離の外や停止目標の先まで立つ(spanを渡さない場合は走査距離と一致する)。
void AccumulateEdges(const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
                     const std::vector<rs::world::EdgeAheadAcrossScenes>& edges,
                     double maxDistanceM, bool scanIsTravelDir, bool stopping,
                     std::unordered_map<rs::world::Guid, StationApproachMask,
                                        rs::world::GuidHash>& out) {
    for (const rs::world::EdgeAheadAcrossScenes& ahead : edges) {
        if (ahead.distance > maxDistanceM) break;
        const rs::world::TrackGraph* graph = graphForScene(ahead.sceneIndex);
        if (graph == nullptr) continue;
        const std::unordered_map<rs::world::Guid, std::vector<rs::world::Guid>, rs::world::GuidHash>::const_iterator
            platformIt = graph->edgeToPlatforms.find(ahead.edge);
        if (platformIt == graph->edgeToPlatforms.end()) continue;

        // 後方走査は走査の向きが走行方向の逆なので、ビットは走行方向基準へ戻す。
        const bool travelAlongEdgeDir = scanIsTravelDir ? ahead.alongEdgeDir : !ahead.alongEdgeDir;
        const bool isApproach1 = (travelAlongEdgeDir == kApproach1IsAlongEdgeDir);

        for (const rs::world::Guid& platformId : platformIt->second) {
            const rs::world::PlatformStopInfo* platform =
                rs::world::FindPlatformById(*graph, platformId);
            if (platform == nullptr) continue;
            if (rs::world::GuidIsNil(platform->stationEntity)) continue;
            if (platform->indexInStation < 0 || platform->indexInStation >= kMaxPlatformBits) continue;
            const uint32_t bit = uint32_t(1) << platform->indexInStation;
            StationApproachMask& mask = out[platform->stationEntity];
            if (isApproach1) mask.approach1 |= bit;
            else mask.approach2 |= bit;
            // RailSim II はレール状態のbit4を「そのレールに重なる編成が停車中(state==2)」で
            // 立てる(CRailWay.cpp:1183/1188)。向きでは分かれないため、方向ビットと
            // 同じ場所で同じ番線へORするだけでよい。
            if (stopping) mask.stopping |= bit;
        }
    }
}

} // namespace

double ApproachBrakingDistanceM(double speedKmh, double maxDecelerationKmhPerS,
                                const ApproachTuning& tuning) {
    const double speedMs = std::fabs(speedKmh) * rs::core::kKilometersPerHourToMetersPerSecond;
    // 制動距離: RailSim II のSTOP_LIMIT_LINE+制動距離の考え方。制動距離は、自動停止がブレーキを
    // 掛け始めるのと同じ制動パターン(立ち上がり・計画減速度・絞り)で計算する。
    // 減速度が0(未指定)ならSTOP_LIMIT_LINEだけに潰れる。
    const double brakingM = SimTuning::Embedded().approach.stopLimitLineM +
                            PatternBrakingDistanceM(speedKmh, maxDecelerationKmhPerS);
    // 定時間: 速度に依らず一定の警報時間を確保する項。速度は必ず絶対値で使う
    // (逆走は負値で来るため、符号付きのままだと負距離になり下限へ潰れる)。
    const double timeBasedM =
        speedMs * std::max(0.0, tuning.warningTimeS) + std::max(0.0, tuning.minDistanceM);
    return std::max(brakingM, timeBasedM);
}

std::unordered_map<rs::world::Guid, StationApproachMask, rs::world::GuidHash>
ComputePlatformApproach(
    const rs::world::TrackGraph& graph, const std::vector<ApproachTrain>& trains,
    const ApproachTuning& tuning) {
    // 単一Scene版はScene跨ぎ版の特殊な場合。空のWarpLinkTableを渡せばワープ先が引けず
    // 走査はそのSceneで終わるので、走査距離やクランプの規則を二重に持たなくて済む。
    const std::function<const rs::world::TrackGraph*(size_t)> graphForScene =
        [&graph](size_t sceneIndex) -> const rs::world::TrackGraph* {
            return sceneIndex == 0 ? &graph : nullptr;
        };
    const rs::world::WarpLinkTable kNoWarpLinks;
    std::vector<ApproachTrain> singleSceneTrains = trains;
    for (ApproachTrain& train : singleSceneTrains) train.front.sceneIndex = 0;
    return ComputePlatformApproachAcrossScenes(graphForScene, kNoWarpLinks, singleSceneTrains,
                                               tuning);
}


std::unordered_map<rs::world::Guid, StationApproachMask, rs::world::GuidHash>
ComputePlatformApproachAcrossScenes(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, const std::vector<ApproachTrain>& trains,
    const ApproachTuning& tuning) {
    const std::vector<rs::world::ForwardScanSpan> noForwardSpans;
    return ComputePlatformApproachAcrossScenes(graphForScene, warpTable, trains, noForwardSpans,
                                               tuning);
}

std::unordered_map<rs::world::Guid, StationApproachMask, rs::world::GuidHash>
ComputePlatformApproachAcrossScenes(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, const std::vector<ApproachTrain>& trains,
    const std::vector<rs::world::ForwardScanSpan>& forwardSpans, const ApproachTuning& tuning) {
    std::unordered_map<rs::world::Guid, StationApproachMask, rs::world::GuidHash> result;

    for (size_t trainIndex = 0; trainIndex < trains.size(); ++trainIndex) {
        const ApproachTrain& train = trains[trainIndex];
        // 起点は常に編成の物理的な先端(front)。front.position.directionは進行方向で、
        // 順走・逆走を区別せずに、前方(先端→進行方向)と在線(先端→進行方向の逆へ全長)の
        // 2回の走査で済む。
        const double trainLengthM = std::max(0.0, train.trainLengthM);
        double forwardM =
            ApproachBrakingDistanceM(train.speedKmh, train.maxDecelerationKmhPerS, tuning);
        // 停止目標があれば、そこで走査を打ち切る。stopDistanceMは先端基準の残り距離で、
        // 走査距離と同じ座標系。距離はワープ跨ぎでも連続に積まれるので、式はそのまま
        // 境界を越えて適用される。
        if (train.stopDistanceM.has_value()) {
            forwardM = std::min(forwardM, std::max(0.0, *train.stopDistanceM));
        }
        rs::world::ForwardScanSpan fallback;
        const rs::world::ForwardScanSpan* forwardSpan = nullptr;
        if (trainIndex < forwardSpans.size() &&
            (forwardM <= forwardSpans[trainIndex].scannedM ||
             forwardSpans[trainIndex].termination.atEnd)) {
            forwardSpan = &forwardSpans[trainIndex];
        } else {
            fallback = rs::world::ScanForwardSpanAcrossScenes(
                graphForScene, warpTable, train.front.sceneIndex, train.front.position, forwardM,
                train.slotOverride);
            forwardSpan = &fallback;
        }
        AccumulateEdges(graphForScene, forwardSpan->entries, forwardM, /*scanIsTravelDir=*/true,
                        train.stopping, result);

        // 編成が今占有している範囲(先端から進行方向の逆へ全長ぶん)を足す。
        // 停止中でも番線上に居ればビットが立つのはこの走査による(クランプしない)。
        rs::world::TrackPosition backwardStart = train.front.position;
        backwardStart.direction = -backwardStart.direction;
        const std::vector<rs::world::EdgeAheadAcrossScenes> backward =
            rs::world::ScanEdgesAheadAcrossScenes(graphForScene, warpTable,
                                                   train.front.sceneIndex, backwardStart,
                                                   trainLengthM, nullptr, train.slotOverride);
        AccumulateEdges(graphForScene, backward, trainLengthM, /*scanIsTravelDir=*/false,
                        train.stopping, result);
    }
    return result;
}

} // namespace rs::sim
