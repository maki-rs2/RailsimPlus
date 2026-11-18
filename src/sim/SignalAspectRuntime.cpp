#include "SignalAspectRuntime.h"

#include <algorithm>

#include "SimTuning.h"
#include "../world/alignment/SpeedSignalForwardScan.h"
#include "../world/alignment/SpeedSignalRegistry.h"
#include "../world/alignment/TrackGraph.h"

namespace rs::sim {

namespace {

// Recompute中の信号1本ぶんの作業用データ。
struct SignalWork {
    const rs::world::SpeedSignal* signal = nullptr;
    size_t sceneIndex = 0;  // 信号のあるシーン
    std::vector<int> speeds;
    bool blocked = false;  // 担当閉塞に在線がある、または強制R
    std::vector<TrainId> occupants;  // 担当閉塞の在線列車
    int forwardIndex = -1; // 前方の信号のwork添字。無ければ-1
    bool deadEndAhead = false;  // 前方に信号が無く、閉塞が続いたまま行き止まりに着く
    bool routeDenied = false;   // 前方の信号(無ければ終端)までに、入ってくる線に開通していない合流がある
    int clearStage = 0;
};

// 行き止まりを前方の赤とみなすときの、前方の信号の速度[km/h]。
constexpr int kDeadEndForwardSpeedKmh = 0;

int DisplayStageOf(const SignalWork& work) {
    return work.blocked || work.routeDenied ? 0 : work.clearStage;
}

// 前方の信号の、いま表示している段の速度。前方の信号が無ければnullopt。
// 前方に信号が無くても、閉塞が続いたまま行き止まりに着くなら、赤の信号がある扱いにする。
std::optional<int> ForwardSpeedKmh(const std::vector<SignalWork>& works, const SignalWork& work) {
    if (work.forwardIndex < 0) {
        return work.deadEndAhead ? std::optional<int>(kDeadEndForwardSpeedKmh) : std::nullopt;
    }
    const SignalWork& forward = works[static_cast<size_t>(work.forwardIndex)];
    return forward.speeds[static_cast<size_t>(DisplayStageOf(forward))];
}

// 計算に入れる信号をworksへ足す。担当閉塞が無い信号と段表が不正な信号は入れない。
void CollectWorks(
    const rs::world::TrackGraph& graph, size_t sceneIndex, const BlockOccupancySnapshot& occupancy,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& forcedRed,
    const SignalStageSpeedsFn& stageSpeeds, std::vector<SignalWork>& works) {
    for (const rs::world::SpeedSignal& signal : graph.speedSignals) {
        if (!rs::world::IsBlockBoundarySignal(signal.kind)) continue;
        const rs::world::Block* block = rs::world::OwnerBlock(graph, signal);
        if (block == nullptr) continue;
        SignalWork work;
        work.signal = &signal;
        work.sceneIndex = sceneIndex;
        work.speeds = stageSpeeds(signal);
        if (!IsValidAspectTable(work.speeds)) continue;
        const BlockOccupancySnapshot::const_iterator occupancyIt = occupancy.find(block->guid);
        if (occupancyIt != occupancy.end()) work.occupants = occupancyIt->second.occupantTrainIds;
        const bool occupied = !work.occupants.empty();
        work.blocked = occupied || forcedRed.find(signal.guid) != forcedRed.end();
        work.clearStage = static_cast<int>(work.speeds.size()) - 1;
        works.push_back(std::move(work));
    }
}

// 各信号の前方の信号を引く。シーン間ワープを跨いで探す。前方の信号が計算に入っていなければ-1のまま。
void LinkForwardSignals(const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
                        const rs::world::WarpLinkTable& warpTable, std::vector<SignalWork>& works) {
    std::unordered_map<rs::world::Guid, int, rs::world::GuidHash> indexByGuid;
    for (size_t index = 0; index < works.size(); ++index) {
        indexByGuid[works[index].signal->guid] = static_cast<int>(index);
    }
    for (SignalWork& work : works) {
        const rs::world::SpeedSignal& signal = *work.signal;
        const rs::world::SpeedSignalForwardResult forward =
            rs::world::SearchForwardSpeedSignalAcrossScenes(
                graphForScene, warpTable, work.sceneIndex,
                rs::world::TrackPosition{signal.edge, signal.s, signal.direction}, +1, signal.guid,
                rs::world::kSpeedSignalForwardSearchM);
        work.deadEndAhead = forward.endsAtDeadEnd;
        work.routeDenied = forward.routeNotAligned;
        if (!forward.ahead.has_value()) continue;
        const std::unordered_map<rs::world::Guid, int, rs::world::GuidHash>::const_iterator it =
            indexByGuid.find(forward.ahead->signal);
        if (it != indexByGuid.end()) work.forwardIndex = it->second;
    }
}

// 全信号を最上段から始め、変化が無くなるまで下げる。段は前方の速度が下がると下がる一方
// (ComputeSignalAspectIndexは前方の速度に対して単調)で、各信号に下限(段0)があるので、
// 環状線で在線が無くても止まる。1周ごとに少なくとも1つの段が1以上下がるので、周回数は
// 「全信号の(段数-1)の合計+1」を超えない。これを反復回数の上限にする。
void SolveClearStages(std::vector<SignalWork>& works) {
    size_t maxPasses = 1;
    for (const SignalWork& work : works) maxPasses += work.speeds.size() - 1;
    for (size_t pass = 0; pass < maxPasses; ++pass) {
        bool changed = false;
        for (SignalWork& work : works) {
            const int next = ComputeSignalAspectIndex(work.speeds, false, ForwardSpeedKmh(works, work));
            if (next == work.clearStage) continue;
            work.clearStage = next;
            changed = true;
        }
        if (!changed) return;
    }
}

// 速度forwardSpeedKmh以下で最も高い段。0の段は必ずあるので、必ず見つかる。
int HighestStageAtOrBelow(const std::vector<int>& speeds, int forwardSpeedKmh) {
    int result = 0;
    for (size_t index = 1; index < speeds.size(); ++index) {
        if (speeds[index] > forwardSpeedKmh) break;
        result = static_cast<int>(index);
    }
    return result;
}

// 表示専用の信号(中継・遠方・入換)の現示を決めて、statesへ入れる。主信号の計算(works)のあとに呼ぶ。
// 中継は前方の主信号が出している速度以下で最も高い段、遠方は前方の主信号の1段手前(ComputeSignalAspectIndex)、
// 入換は最上段で強制Rなら段0。前方の主信号が無ければ最上段、行き止まりが続くなら赤の前方として扱う。
void ComputeDisplayOnlyStates(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene, size_t sceneCount,
    const rs::world::WarpLinkTable& warpTable, const std::vector<SignalWork>& works,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& forcedRed,
    const SignalStageSpeedsFn& stageSpeeds,
    std::unordered_map<rs::world::Guid, SignalAspectState, rs::world::GuidHash>& states) {
    std::unordered_map<rs::world::Guid, size_t, rs::world::GuidHash> workIndex;
    for (size_t index = 0; index < works.size(); ++index) workIndex[works[index].signal->guid] = index;

    for (size_t sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex) {
        const rs::world::TrackGraph* graph = graphForScene(sceneIndex);
        if (graph == nullptr) break;
        for (const rs::world::SpeedSignal& signal : graph->speedSignals) {
            if (rs::world::IsBlockBoundarySignal(signal.kind)) continue;
            const std::vector<int> speeds = stageSpeeds(signal);
            if (!IsValidAspectTable(speeds)) continue;
            const int topStage = static_cast<int>(speeds.size()) - 1;
            int stage = topStage;
            if (signal.kind == rs::world::SpeedSignalKind::Shunting) {
                if (forcedRed.contains(signal.guid)) stage = 0;
            } else {
                const rs::world::SpeedSignalForwardResult forward =
                    rs::world::SearchForwardSpeedSignalAcrossScenes(
                        graphForScene, warpTable, sceneIndex,
                        rs::world::TrackPosition{signal.edge, signal.s, signal.direction}, +1, signal.guid,
                        rs::world::kSpeedSignalForwardSearchM);
                std::optional<int> forwardSpeedKmh;
                if (forward.ahead.has_value()) {
                    const auto it = workIndex.find(forward.ahead->signal);
                    if (it != workIndex.end()) {
                        const SignalWork& main = works[it->second];
                        forwardSpeedKmh = main.speeds[static_cast<size_t>(DisplayStageOf(main))];
                    }
                } else if (forward.endsAtDeadEnd) {
                    forwardSpeedKmh = kDeadEndForwardSpeedKmh;
                }
                if (forwardSpeedKmh.has_value()) {
                    stage = signal.kind == rs::world::SpeedSignalKind::Relay
                                ? HighestStageAtOrBelow(speeds, *forwardSpeedKmh)
                                : ComputeSignalAspectIndex(speeds, false, forwardSpeedKmh);
                }
            }
            SignalAspectState state;
            state.clearStage = stage;
            state.displayStage = stage;
            states[signal.guid] = std::move(state);
        }
    }
}

} // namespace

bool IsValidAspectTable(const std::vector<int>& speeds) {
    if (speeds.empty() || speeds.front() != 0) return false;
    for (size_t index = 1; index < speeds.size(); ++index) {
        if (speeds[index - 1] >= speeds[index]) return false;
    }
    return true;
}

int ComputeSignalAspectIndex(const std::vector<int>& speeds, bool blockOccupiedOrForcedRed,
                             std::optional<int> forwardAspectSpeedKmh) {
    if (blockOccupiedOrForcedRed) return 0;
    const int topIndex = static_cast<int>(speeds.size()) - 1;
    if (!forwardAspectSpeedKmh.has_value()) return topIndex;

    int matchingIndex = 0;
    for (size_t index = 1; index < speeds.size(); ++index) {
        if (speeds[index] > *forwardAspectSpeedKmh) break;
        matchingIndex = static_cast<int>(index);
    }
    return std::min(matchingIndex + 1, topIndex);
}

int StageForTrain(int clearStage, bool forcedRed, bool routeDenied,
                  const std::vector<TrainId>& occupants, TrainId trainId) {
    if (forcedRed || routeDenied) return 0;
    const bool otherTrainPresent =
        std::any_of(occupants.begin(), occupants.end(),
                    [&trainId](const TrainId& occupant) { return occupant != trainId; });
    return otherTrainPresent ? 0 : clearStage;
}

void SignalAspectRuntime::Clear() {
    states_.clear();
    manualForcedRed_.clear();
    manualEntryPermit_.clear();
}

void SignalAspectRuntime::SetForceRed(const rs::world::Guid& signalGuid, bool forced) {
    if (forced) {
        manualForcedRed_.insert(signalGuid);
    } else {
        manualForcedRed_.erase(signalGuid);
    }
}

bool SignalAspectRuntime::IsForcedRed(const rs::world::Guid& signalGuid) const {
    return manualForcedRed_.find(signalGuid) != manualForcedRed_.end();
}

void SignalAspectRuntime::SetEntryPermit(const rs::world::Guid& signalGuid, bool permitted) {
    if (permitted) {
        manualEntryPermit_.insert(signalGuid);
    } else {
        manualEntryPermit_.erase(signalGuid);
    }
}

bool SignalAspectRuntime::IsEntryPermitted(const rs::world::Guid& signalGuid) const {
    return manualEntryPermit_.find(signalGuid) != manualEntryPermit_.end();
}

std::vector<int> SignalStageSpeeds(const rs::world::SpeedSignal& signal) {
    std::vector<int> speeds;
    speeds.reserve(signal.stages.size());
    for (const rs::world::SignalAspectStage& stage : signal.stages) speeds.push_back(stage.speedKmh);
    return speeds;
}

void SignalAspectRuntime::Recompute(const rs::world::TrackGraph& graph,
                                    const BlockOccupancySnapshot& occupancy,
                                    const SignalStageSpeedsFn& stageSpeeds) {
    const std::function<const rs::world::TrackGraph*(size_t)> graphForScene =
        [&graph](size_t sceneIndex) -> const rs::world::TrackGraph* {
        return sceneIndex == 0 ? &graph : nullptr;
    };
    Recompute(graphForScene, 1, rs::world::WarpLinkTable{}, occupancy, stageSpeeds);
}

void SignalAspectRuntime::Recompute(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene, size_t sceneCount,
    const rs::world::WarpLinkTable& warpTable, const BlockOccupancySnapshot& occupancy,
    const SignalStageSpeedsFn& stageSpeeds) {
    states_.clear();
    std::vector<SignalWork> works;
    for (size_t sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex) {
        const rs::world::TrackGraph* graph = graphForScene(sceneIndex);
        if (graph == nullptr) break;
        CollectWorks(*graph, sceneIndex, occupancy, manualForcedRed_, stageSpeeds, works);
    }
    LinkForwardSignals(graphForScene, warpTable, works);
    SolveClearStages(works);

    for (const SignalWork& work : works) {
        SignalAspectState state;
        state.clearStage = work.clearStage;
        state.displayStage = DisplayStageOf(work);
        state.routeDenied = work.routeDenied;
        state.occupants = work.occupants;
        states_[work.signal->guid] = std::move(state);
    }
    ComputeDisplayOnlyStates(graphForScene, sceneCount, warpTable, works, manualForcedRed_, stageSpeeds,
                             states_);
}

const SignalAspectState* SignalAspectRuntime::Find(const rs::world::Guid& signalGuid) const {
    const std::unordered_map<rs::world::Guid, SignalAspectState, rs::world::GuidHash>::const_iterator
        it = states_.find(signalGuid);
    return it == states_.end() ? nullptr : &it->second;
}

std::optional<int> SignalAspectRuntime::StageForTrainAt(const rs::world::Guid& signalGuid,
                                                        TrainId trainId) const {
    const SignalAspectState* state = Find(signalGuid);
    if (state == nullptr) return std::nullopt;
    return StageForTrain(state->clearStage, IsForcedRed(signalGuid), state->routeDenied,
                         state->occupants, trainId);
}

rs::world::SignalSpeedForTrainFn MakeSignalSpeedForTrainFn(
    const SignalAspectRuntime& runtime, TrainId trainId,
    const std::unordered_set<rs::world::Guid, rs::world::GuidHash>* deniedSignals) {
    return [&runtime, trainId, deniedSignals](
               const rs::world::SpeedSignal& signal) -> std::optional<double> {
        // この列車がまだ進路を取れていない信号は、進入許可があっても停止にする。
        if (deniedSignals != nullptr && deniedSignals->contains(signal.guid)) return 0.0;
        const std::optional<int> stage = runtime.StageForTrainAt(signal.guid, trainId);
        if (!stage.has_value() || *stage < 0 || static_cast<size_t>(*stage) >= signal.stages.size()) {
            return std::nullopt;
        }
        const SignalAspectState* state = runtime.Find(signal.guid);
        if (*stage == 0 && state != nullptr && !state->routeDenied &&
            runtime.IsEntryPermitted(signal.guid)) {
            return SimTuning::Embedded().signalMargins.entryPermitSpeedKmh;
        }
        return static_cast<double>(signal.stages[static_cast<size_t>(*stage)].speedKmh);
    };
}

SignalDisplay ResolveSignalDisplay(const SignalAspectRuntime* runtime, const rs::world::SpeedSignal& signal) {
    SignalDisplay display;
    const SignalAspectState* state = runtime != nullptr ? runtime->Find(signal.guid) : nullptr;
    if (state != nullptr && state->displayStage >= 0 &&
        static_cast<size_t>(state->displayStage) < signal.stages.size()) {
        display.stage = state->displayStage;
        display.speedKmh = signal.stages[static_cast<size_t>(state->displayStage)].speedKmh;
        return display;
    }
    display.speedKmh = rs::world::SignalTopSpeedKmh(signal);
    return display;
}

} // namespace rs::sim
