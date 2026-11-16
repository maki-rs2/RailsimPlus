#include "Rs2TrainApplier.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <iterator>

#include "FormationBuilder.h"
#include "PluginSelector.h"
#include "Rs2TrainRunStateRestore.h"
#include "../../sim/PlatformPassThrough.h"
#include "../scene/TrainPlacement.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/FormationRoutineResolve.h"
#include "../../world/alignment/PlatformRegistry.h"
#include "../../world/alignment/TrackQuery.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../../world/scene/TrainTagRegistry.h"

namespace rs::app {

namespace {

// .rs2取り込み編成をPos/Dirから軌道へスナップする際の許容距離[m]。
// これを超える最近傍点は「別Sceneの軌道を誤って拾った」とみなし配置しない。
constexpr double kSnapMaxDistanceM = 5.0;

// GroupEnd0/1が乗っているRailWayのAddressをヒントにScene添字を決める。どちらも解決できなければ
// 全Sceneへ先頭車のPosでスナップを試し、距離最小のSceneを返す。
// 見つからなければ-1を返す。
int ResolveTargetSceneIndex(const rs::legacy_layout::Rs2TrainGroup& group,
                            const std::vector<rs::world::TrackGraph*>& sceneGraphs,
                            const std::vector<rs::legacy_layout::AddressMap>& sceneAddresses) {
    for (const rs::legacy_layout::Rs2GroupEndLocator& end : group.runState.groupEnd) {
        const rs::legacy_layout::Rs2Address& hint = end.railAddress;
        if (hint.empty()) continue;
        for (size_t i = 0; i < sceneAddresses.size(); ++i) {
            if (!rs::world::GuidIsNil(sceneAddresses[i].ResolveWay(hint))) return static_cast<int>(i);
        }
    }

    if (group.trains.empty()) return -1;
    const rs::legacy_layout::Rs2Train& head = group.trains.front();

    int bestIndex = -1;
    double bestDist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < sceneGraphs.size(); ++i) {
        if (!sceneGraphs[i]) continue;
        const rs::world::NearestTrackHit hit =
            rs::world::FindNearestTrackPosition(*sceneGraphs[i], head.pos, head.dir, kSnapMaxDistanceM);
        if (hit.found && hit.distanceM < bestDist) {
            bestDist = hit.distanceM;
            bestIndex = static_cast<int>(i);
        }
    }
    return bestIndex;
}

// group.addressを参照する未解決のポイントルーチンを、formationIdをキーにした
// 編成定義別の層(TrackNode::pointFormationRoutines)へ書き込む。Enabled判定より前に
// 全TrainGroupに対して呼ぶ(Enabled=noの編成でも編成定義自体は必ず作られるため)。
void AttachPendingPointRoutinesToFormation(
    const std::vector<rs::legacy_layout::Rs2PendingTrainRoutines>& pendingRoutines,
    std::vector<std::vector<bool>>& consumed, const std::vector<rs::world::TrackGraph*>& sceneGraphs,
    const rs::legacy_layout::Rs2Address& groupAddress, const rs::world::Guid& formationId,
    std::vector<std::string>& warnings) {
    for (size_t sceneIndex = 0; sceneIndex < pendingRoutines.size(); ++sceneIndex) {
        if (sceneIndex >= sceneGraphs.size() || sceneGraphs[sceneIndex] == nullptr) continue;
        const std::vector<rs::legacy_layout::PendingPointTrainRoutine>& entries =
            pendingRoutines[sceneIndex].points;
        for (size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
            const rs::legacy_layout::PendingPointTrainRoutine& entry = entries[entryIndex];
            if (entry.trainGroup != groupAddress || consumed[sceneIndex][entryIndex]) continue;
            const std::unordered_map<rs::world::Guid, rs::world::TrackNode, rs::world::GuidHash>::iterator nodeIt =
                sceneGraphs[sceneIndex]->nodes.find(entry.node);
            if (nodeIt == sceneGraphs[sceneIndex]->nodes.end()) {
                warnings.push_back("編成別ポイントルーチンの対象nodeが存在しないため破棄: " +
                                   entry.trainGroup);
            } else {
                nodeIt->second.pointFormationRoutines.push_back({formationId, entry.routine});
            }
            consumed[sceneIndex][entryIndex] = true;
        }
    }
}

// AttachPendingPointRoutinesToFormationの番線版。
void AttachPendingPlatformRoutinesToFormation(
    const std::vector<rs::legacy_layout::Rs2PendingTrainRoutines>& pendingRoutines,
    std::vector<std::vector<bool>>& consumed, const std::vector<rs::world::TrackGraph*>& sceneGraphs,
    const rs::legacy_layout::Rs2Address& groupAddress, const rs::world::Guid& formationId,
    std::vector<std::string>& warnings) {
    for (size_t sceneIndex = 0; sceneIndex < pendingRoutines.size(); ++sceneIndex) {
        if (sceneIndex >= sceneGraphs.size() || sceneGraphs[sceneIndex] == nullptr) continue;
        const std::vector<rs::legacy_layout::PendingPlatformTrainRoutine>& entries =
            pendingRoutines[sceneIndex].platforms;
        for (size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
            const rs::legacy_layout::PendingPlatformTrainRoutine& entry = entries[entryIndex];
            if (entry.trainGroup != groupAddress || consumed[sceneIndex][entryIndex]) continue;
            rs::world::PlatformStopInfo* platform =
                rs::world::FindPlatformById(*sceneGraphs[sceneIndex], entry.platform);
            if (platform == nullptr) {
                warnings.push_back("編成別停車ルーチンの対象番線が存在しないため破棄: " +
                                   entry.trainGroup);
            } else {
                platform->formationRoutines.push_back({formationId, entry.routine});
            }
            consumed[sceneIndex][entryIndex] = true;
        }
    }
}

// 保存時点で駅に停まっていた編成を、足元の番線つきの停車状態として復元用の出力へ追加する。
void AppendStoppedTrainRestore(const rs::legacy_layout::Rs2TrainGroup& group, TrainId trainId,
                               size_t sceneIndex, const std::vector<rs::world::TrackGraph*>& sceneGraphs,
                               const rs::world::TrackPosition& head,
                               const std::vector<FormationCar>& cars,
                               const Rs2TrainRestoreContext& context,
                               std::vector<std::string>& warnings) {
    if (context.runtime == nullptr || context.warpLinks == nullptr) return;
    if (!IsStoppedRunState(group.runState)) return;
    double trainLengthM = 0.0;
    double doorClosingTimeSeconds = 0.0;  // 編成内の最大値(RailSim II の CalcSpec と同じ)
    for (const FormationCar& car : cars) {
        trainLengthM += car.frontLimit - car.tailLimit;
        doorClosingTimeSeconds = std::max(doorClosingTimeSeconds, car.doorClosingTime);
    }

    const rs::sim::GraphForScene graphForScene =
        [&sceneGraphs](size_t index) -> const rs::world::TrackGraph* {
        return index < sceneGraphs.size() ? sceneGraphs[index] : nullptr;
    };
    const rs::world::Guid platformId = rs::sim::FindStoppablePlatformUnderTrain(
        graphForScene, *context.warpLinks, sceneIndex, head, trainLengthM);
    const rs::world::PlatformStopInfo* platform =
        rs::world::GuidIsNil(platformId) ? nullptr
                                         : rs::world::FindPlatformById(*sceneGraphs[sceneIndex], platformId);
    const std::optional<Rs2TrainRestore> restore = BuildRs2TrainRestore(
        group.runState, trainId, sceneIndex, platform, context.nowAbsSeconds,
        rs::sim::DiaRuntime::DoorCloseLeadSeconds(doorClosingTimeSeconds,
                                                  RunStateOpensAnyDoor(group.runState),
                                                  context.timeScale),
        group.name, warnings);
    if (!restore.has_value()) return;
    context.runtime->trains.push_back(restore->train);
    context.runtime->dia.groups.push_back(restore->dia);
}

} // namespace

Rs2TrainApplyResult ApplyRs2TrainGroups(const rs::legacy_layout::Rs2Layout& layout,
                                        const std::string& installRootUtf8,
                                        PluginSelector& selector,
                                        const std::vector<rs::world::TrackGraph*>& sceneGraphs,
                                        const std::vector<rs::legacy_layout::AddressMap>& sceneAddresses,
                                        const std::vector<rs::legacy_layout::Rs2PendingTrainRoutines>& pendingTrainRoutines,
                                        std::vector<rs::world::SceneEntity>& cars,
                                        std::vector<TrainId>* placedTrainIds,
                                        std::vector<TrainSpeedSeed>* speedSeeds,
                                        rs::world::TrainTagRegistry* trainFormationLinks,
                                        const Rs2TrainRestoreContext& restoreContext) {
    Rs2TrainApplyResult result;
    // 呼び出し側が受け取らない場合の捨て先(取り込み直後のタグは空なので、対応だけが入る)。
    rs::world::TrainTagRegistry discardedLinks;
    rs::world::TrainTagRegistry& linkRegistry =
        trainFormationLinks != nullptr ? *trainFormationLinks : discardedLinks;
    std::vector<std::vector<bool>> consumedPoints;
    std::vector<std::vector<bool>> consumedPlatforms;
    consumedPoints.reserve(pendingTrainRoutines.size());
    consumedPlatforms.reserve(pendingTrainRoutines.size());
    for (const rs::legacy_layout::Rs2PendingTrainRoutines& entries : pendingTrainRoutines) {
        consumedPoints.emplace_back(entries.points.size(), false);
        consumedPlatforms.emplace_back(entries.platforms.size(), false);
    }

    for (const rs::legacy_layout::Rs2TrainGroup& group : layout.trainGroups) {
        std::vector<FormationCarSpec> formationCars;
        formationCars.reserve(group.trains.size());
        for (const rs::legacy_layout::Rs2Train& t : group.trains)
            formationCars.push_back(FormationCarSpec{t.trainPlugin, t.reverse, t.switchOptions});

        size_t defIndex = 0;
        const size_t addedCount = BuildFormationFromPlugins(selector, group.name, formationCars,
                                                              installRootUtf8, result.warnings, &defIndex);
        if (addedCount > 0) ++result.formationsAdded;
        if (defIndex >= selector.Formations().size()) continue;
        const rs::world::Guid formationId = selector.Formations()[defIndex].guid;

        // Enabled判定より前に、この編成定義を参照する未解決ルーチンを編成定義別の層へ
        // 書き込む(Enabled=noでも編成定義自体は上で必ず作られているため)。
        AttachPendingPointRoutinesToFormation(pendingTrainRoutines, consumedPoints, sceneGraphs,
                                              group.address, formationId, result.warnings);
        AttachPendingPlatformRoutinesToFormation(pendingTrainRoutines, consumedPlatforms, sceneGraphs,
                                                 group.address, formationId, result.warnings);

        if (!group.enabled) continue;
        if (group.trains.empty()) {
            result.warnings.push_back("TrainGroupにTrainがありません(Enabled=yes): " + group.address);
            continue;
        }

        const int sceneIndex = ResolveTargetSceneIndex(group, sceneGraphs, sceneAddresses);
        if (sceneIndex < 0) {
            result.warnings.push_back("TrainGroupの配置先Sceneを特定できません: " + group.name);
            continue;
        }
        rs::world::TrackGraph* graph = sceneGraphs[static_cast<size_t>(sceneIndex)];
        if (!graph) {
            result.warnings.push_back("TrainGroupの配置先Sceneが無効です: " + group.name);
            continue;
        }

        const rs::legacy_layout::Rs2Train& headTrain = group.trains.front();
        const rs::world::NearestTrackHit headHit =
            rs::world::FindNearestTrackPosition(*graph, headTrain.pos, headTrain.dir, kSnapMaxDistanceM);
        if (!headHit.found) {
            result.warnings.push_back("TrainGroupを軌道へスナップできません: " + group.name);
            continue;
        }

        const std::vector<FormationCar>& defCars = selector.Formations()[defIndex].cars;
        const std::vector<FormationCar>::const_iterator headCarIt = std::find_if(
            defCars.begin(), defCars.end(), [](const FormationCar& car) { return !car.unresolved; });
        if (headCarIt == defCars.end()) {
            result.warnings.push_back("TrainGroupの車両を1両も読み込めませんでした: " + group.name);
            continue;
        }

        // .rs2のPosは車両モデルの原点だが、PlaceTrainRepeatingが要求するheadは
        // 先頭車のFrontLimit位置(連結面)。原点から進行方向前方へその分だけ進める
        // (反転車両はモデルの-Z側が進行方向前方を向くためtailLimit側を使う)。
        const FormationCar& headCar = *headCarIt;
        const double headOffsetM = headCar.reversed ? -headCar.tailLimit : headCar.frontLimit;
        const rs::world::TrackPosition head =
            rs::world::Advance(*graph, headHit.position, headOffsetM).pos;

        TrainId trainId = rs::world::NilGuid();
        std::vector<rs::world::SceneEntity> placedEntities = BuildTrainRepeating(
            defCars, head, *graph, static_cast<size_t>(sceneIndex), &trainId);
        const int placedCars = static_cast<int>(placedEntities.size());
        if (placedCars > 0) {
            cars.insert(cars.end(), std::make_move_iterator(placedEntities.begin()),
                            std::make_move_iterator(placedEntities.end()));
            ++result.trainsPlaced;
            result.carsPlaced += static_cast<size_t>(placedCars);
            if (placedTrainIds != nullptr) placedTrainIds->push_back(trainId);
            if (group.runState.present && speedSeeds != nullptr) {
                // .rs2のTargetSpeed/CurrentSpeedは符号付きで、負値がそのまま後退を表す
                // (RailSim IIの CTrainGroup::m_TargetSpeed。折返は符号反転で表現される)。
                // TrainGroupのReverseは「どちら側の端が今の先頭か」を速度の符号から
                // 導出して保存した値なので、ここで符号として掛けてはいけない
                // (掛けると後退中の編成が二重否定で前進向きに復元される)。
                speedSeeds->push_back({trainId, group.runState.targetSpeed, group.runState.currentSpeed});
            }
            AppendStoppedTrainRestore(group, trainId, static_cast<size_t>(sceneIndex), sceneGraphs, head,
                                      defCars, restoreContext, result.warnings);
            rs::world::RegisterPlacedTrainFormation(formationId, trainId, linkRegistry);
        }
    }

    for (size_t sceneIndex = 0; sceneIndex < pendingTrainRoutines.size(); ++sceneIndex) {
        const std::vector<rs::legacy_layout::PendingPointTrainRoutine>& entries = pendingTrainRoutines[sceneIndex].points;
        for (size_t entryIndex = 0; entryIndex < entries.size(); ++entryIndex) {
            if (!consumedPoints[sceneIndex][entryIndex]) {
                result.warnings.push_back("編成別ポイントルーチンの参照先TrainGroupに対応する編成定義が無いため破棄: " +
                                          entries[entryIndex].trainGroup);
            }
        }
        const std::vector<rs::legacy_layout::PendingPlatformTrainRoutine>& platformEntries =
            pendingTrainRoutines[sceneIndex].platforms;
        for (size_t entryIndex = 0; entryIndex < platformEntries.size(); ++entryIndex) {
            if (!consumedPlatforms[sceneIndex][entryIndex]) {
                result.warnings.push_back("編成別停車ルーチンの参照先TrainGroupに対応する編成定義が無いため破棄: " +
                                          platformEntries[entryIndex].trainGroup);
            }
        }
    }

    return result;
}

} // namespace rs::app
