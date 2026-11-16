#include "BlockSpeedLimit.h"

#include <algorithm>
#include <vector>

#include "BlockRegistry.h"
#include "SpeedSignalRegistry.h"
#include "TravelDirectionFollow.h"

namespace rs::world {
namespace {

// -1(制限なし)を無限大として小さい方を選ぶ。
float MinLimit(float edgeLimit, float signalLimit) {
    if (edgeLimit < 0.0f) return signalLimit;
    return std::min(edgeLimit, signalLimit);
}

// 進行方向と同じ向きの信号のうち最小の制限速度[km/h]を返す。無ければ負。
// 向きは、信号からつながりをたどって決める(MarkSignalDirectionInBlock)。
// このedgeに着く信号が1つも無いときは制限しない(-1)。着く信号があっても進行方向と同じ向きの
// ものが1つも無いとき(信号の無い向きから入る列車)は、閉塞に入れないよう0を返す。
// 両方向の印が立つedgeは、どちらの向きにも同じ向きとして扱う。
// 現示を持たない信号(signalSpeedがnullopt)があるだけなら制限しない。
double SignalLimitForTravel(const std::vector<const SpeedSignal*>& signals,
                            const std::vector<TravelDirectionMasks>& masks, const Guid& edgeGuid,
                            bool alongEdgeDir, const SignalSpeedForTrainFn& signalSpeed) {
    const unsigned char bit = alongEdgeDir ? kTravelAlongBit : kTravelAgainstBit;
    double limit = -1.0;
    bool hasReachingSignal = false;
    bool hasSameDirectionSignal = false;
    for (size_t index = 0; index < signals.size(); ++index) {
        const TravelDirectionMasks::const_iterator it = masks[index].find(edgeGuid);
        if (it == masks[index].end()) continue;
        hasReachingSignal = true;
        if ((it->second & bit) == 0) continue;
        hasSameDirectionSignal = true;
        const std::optional<double> speedKmh = signalSpeed(*signals[index]);
        if (!speedKmh.has_value()) continue;  // 現示が無い信号は制限をかけない
        const double signalKmh = *speedKmh;
        limit = limit < 0.0 ? signalKmh : std::min(limit, signalKmh);
    }
    if (!hasReachingSignal) return -1.0;
    if (!hasSameDirectionSignal) return 0.0;
    return limit;
}

} // namespace

int SignalTravelDirectionOnEdge(const TrackGraph& graph, const SpeedSignal& signal, const Block& block,
                                const Guid& edgeGuid, const WarpLinkTable* warpTable,
                                size_t sceneIndex) {
    const TravelDirectionMasks masks =
        MarkSignalDirectionInBlock(graph, warpTable, sceneIndex, signal, block);
    const TravelDirectionMasks::const_iterator it = masks.find(edgeGuid);
    if (it == masks.end()) return 0;
    if (it->second == kTravelAlongBit) return +1;
    if (it->second == kTravelAgainstBit) return -1;
    return 0;
}

float EffectiveSpeedLimitKmh(const TrackGraph& graph, const Guid& edgeGuid,
                             const TrackEdge& edge, bool alongEdgeDir,
                             const SignalSpeedForTrainFn& signalSpeed,
                             const WarpLinkTable* warpTable, size_t sceneIndex) {
    const float result = edge.speedLimitKmh;
    if (!signalSpeed || graph.speedSignals.empty()) return result;
    // edgeにかかる全閉塞の信号をまとめて1回だけ判定する。閉塞ごとに判定すると、重なる閉塞の
    // 一方に同じ向きの信号が無いだけで速度0になってしまう。
    std::vector<const SpeedSignal*> signals;
    std::vector<TravelDirectionMasks> masks;
    for (const Block* block : FindBlocksOnEdge(graph, edgeGuid)) {
        for (const SpeedSignal* signal : SpeedSignalsOfBlock(graph, *block)) {
            signals.push_back(signal);
            masks.push_back(MarkSignalDirectionInBlock(graph, warpTable, sceneIndex, *signal, *block));
        }
    }
    if (signals.empty()) return result;
    const double signalKmh = SignalLimitForTravel(signals, masks, edgeGuid, alongEdgeDir, signalSpeed);
    if (signalKmh < 0.0) return result;
    return MinLimit(result, static_cast<float>(signalKmh));
}

} // namespace rs::world
