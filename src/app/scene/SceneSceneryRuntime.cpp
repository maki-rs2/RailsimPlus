#include "../gpu/App.h"
#include "SceneSceneryRuntime.h"
#include "../../render/RenderPrecision.h"
#include "ParsedPluginCache.h"

#include "../../platform/EnvironmentVariable.h"
#include <chrono>
#include <cstring>
#include <cstdlib>
#include "PlatformParentDriver.h"
#include "../plugin_session/PluginRuntimeApplier.h"
#include "../plugin_session/SessionFrameStep.h"
#include "../../plugins/load/ModelMeshHelpers.h"
#include "../../plugins/load/runtime/SoundEffectWorldTracker.h"
#include "../../plugins/load/runtime/StretchZyRuntime.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/scene/ScenePlacement.h"
#include <spdlog/spdlog.h>
namespace rs::app {
namespace {
// `_CAMDIST`を参照するスロットだけ、配置のワールド位置(行列の平行移動成分)から視点距離を
// 更新する。参照しないスロットでは配置行列の計算ごと省く。量子化で値が変わらなければ
// RefreshSwitchGeneration()が世代を進めないので、毎tick呼んでも再ベイクは起きない。
// RefreshSwitchGeneration()より前に呼ぶこと。
void UpdateSceneryCamDist(PluginSession& session, const rs::world::SceneEntity& entity,
                          const rs::world::TrackGraph& graph, const float eye[3],
                          float viewerFovYRadians) {
    if (!session.ReferencesCamDist()) return;
    const rs::core::Mat4 world = rs::world::ResolveWorldMatrix(entity.placement, graph);
    const float unitOrigin[3] = {world.m[12], world.m[13], world.m[14]};
    session.UpdateCamDist(eye, unitOrigin, viewerFovYRadians);
}

// ChangeModelでジオメトリが差し替わったスロット(または未アップロードのスロット)の
// GPUメッシュを再アップロードする。
void UploadSceneryGeometryIfNeeded(App& app, SceneSlotPool::Slot& slot, size_t slotIndex) {
    PluginSession& session = slot.session;
    if (slot.sceneryGpuMeshUploadFailed || !(slot.sceneryGeometryDirty || !slot.hasGpuMesh)) return;
    if (session.plugin.assemblyMesh.vertices.empty() ||
        session.plugin.assemblyMesh.indices.empty()) {
        slot.sceneryGpuMeshUploadFailed = true;
        return;
    }
    const std::string geometryKey =
        MakePluginMeshKey(slot.residency.contentKey) + "|" + session.ResolvedGeometrySignature();
    if (app.UploadSceneMeshToGpu(static_cast<UINT>(slotIndex), geometryKey,
                                 session.SceneUploadMesh())) {
        slot.hasGpuMesh = true;
        slot.sceneryGeometryDirty = false;
    } else {
        slot.sceneryGpuMeshUploadFailed = true;
    }
}

// 現在のパーツ行列を焼き込んだメッシュから、ピッキング用のAABBを更新する。
void UpdateSceneryPickAabb(SceneSlotPool::Slot& slot,
                           const std::vector<rs::core::Mat4>& dynamicWorlds) {
    PluginSession& session = slot.session;
    const rs::render::Mesh& assemblyMesh = session.plugin.assemblyMesh;
    const std::vector<rs::core::Mat4> assemblyPartWorlds =
        rs::legacy_plugin::GatherAssemblyPartWorlds(session.plugin, dynamicWorlds);
    size_t skippedPickVertices = 0;
    rs::legacy_plugin::ComputeBakedAabb(assemblyMesh, assemblyPartWorlds,
                                        slot.sceneryPick.sceneryPickAabbMin,
                                        slot.sceneryPick.sceneryPickAabbMax, &skippedPickVertices);
    // デバッグ用: 範囲外partIndexで無変換のまま残った頂点があると、モデル
    // 作成時のローカル座標がそのままAABBへ混入し歪む(BakeStaticPartWorldsの
    // コメント参照)。当たり判定AABBが実モデルより一回り大きく偏る原因の調査に使う。
    if (skippedPickVertices > 0) {
        spdlog::warn("[scenepicking][debug] entity={} folder={} bakedPickMeshの頂点{}件が"
                    "partIndex範囲外で無変換のままAABBへ算入されています(全{}頂点中)",
                    rs::world::GuidToHex(slot.entityId), slot.residency.folderUtf8, skippedPickVertices,
                    assemblyMesh.vertices.size());
    }
    slot.sceneryPick.sceneryPickHasAabb = !assemblyMesh.vertices.empty();
    slot.sceneryPick.sceneryPickAabbCached = true;
}
}
void SceneSceneryRuntime::Tick(App& app, rs::world::SceneRegistry& registry, rs::world::TrackGraph* activeGraph, std::vector<SceneSlotPool::Slot>& slots, uint64_t activeGraphRevision, double dt, const std::unordered_map<rs::world::Guid, rs::sim::StationApproachMask, rs::world::GuidHash>& approachMasks, const rs::world::GameClock& clock, const rs::world::SolarState& sun, double latitudeDeg, double nightThresholdSin, const float eye[3], float viewerFovYRadians) {
    if (!activeGraph) return; // 呼び出し側が前提を破っている(通常起きない、防御的return)

    using ProfileClock = std::chrono::steady_clock;
    static const bool kProfileFrame = rs::platform::ReadEnvironmentVariable("RS_PROFILE_FRAME") != nullptr;
    const ProfileClock::time_point tickStart = ProfileClock::now();
    std::chrono::nanoseconds lookupTime{};
    std::chrono::nanoseconds worldMatrixTime{};
    std::chrono::nanoseconds settledTime{};
    std::chrono::nanoseconds poseTime{};
    std::chrono::nanoseconds uploadTime{};
    size_t eligibleSlots = 0;
    size_t skippedSlots = 0;
    lastTickRecomputedSlots_ = 0;
    // 親の駅を削除された番線のレールは、最後の姿勢で止まらず休止姿勢へ戻す。
    ResetOrphanedParentDeltas(*activeGraph, [&registry](const rs::world::Guid& guid) {
        return registry.FindEntity(guid) != nullptr;
    });
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].used || slots[i].isTrainCar) continue;
        ++eligibleSlots;
        const ProfileClock::time_point lookupStart = ProfileClock::now();
        const rs::world::SceneEntity* e = registry.FindEntity(slots[i].entityId);
        lookupTime += ProfileClock::now() - lookupStart;
        if (!e) continue; // Sync()未反映の1フレームだけ起こりうる

        SceneSlotPool::Slot& slot = slots[i];
        PluginSession& session = slot.session;

        // animationClockSecondsは下のスキップ判定に関わらず必ず毎フレーム進める。
        // 再生位相の実際の反映(PartAnimationRuntime::Update呼び出し)はここでは
        // 行わず、UpdateSceneAnimations()が描画フレームごとに1回まとめて行う
        // (Tick()自体は1フレームに複数回走りうる固定tickループのため、ここで
        // 反映すると同一フレームで重複書き込みが起きる)。
        session.animationClockSeconds += dt;

        // 時刻・影・環境マップ由来のシステムスイッチ(_YEAR〜_SEASON、_NIGHT、_SHADOW、_ENVMAP。単体プラグイン表示
        // AppRuntimeFrameSim.cppやCar編成SceneTrainRenderSubmit.cppと同じ呼び出し)。
        // 不変スキップ判定より前に置き、スイッチ変化だけのフレームを取りこぼさない
        // (UpdateStationSwitchesを直後に置く理由と同じ)。
        session.UpdateTimeSwitches(clock, sun, latitudeDeg, nightThresholdSin, app.ShadowsEnabled(),
                                  app.EnvMapEnabled(), !e->isTemporary || writeCalendarForTemporary_);
        // 視点距離の`_CAMDIST`も同じ理由で不変スキップより前に更新する
        // (列車と同じ UpdateCamDist 呼び出し)。
        UpdateSceneryCamDist(session, *e, *activeGraph, eye, viewerFovYRadians);

        // 駅舎の接近判定スイッチ。下の不変スキップは
        // RefreshSwitchGeneration()より前に走るため、スイッチ値だけが変わったフレームを
        // ここで拾ってpartWorldsDirtyを立てないと、静止中の可動部(閉じたままの遮断棒等)が
        // 永久に動き出さない。マスクに載っていない駅舎には0を書き、列車が去れば元へ戻す。
        if (!e->sourceAddress.empty()) {
            const std::unordered_map<rs::world::Guid, rs::sim::StationApproachMask,
                                     rs::world::GuidHash>::const_iterator
                maskIt = approachMasks.find(e->guid);
            const rs::sim::StationApproachMask mask =
                (maskIt != approachMasks.end()) ? maskIt->second : rs::sim::StationApproachMask{};
            if (session.UpdateStationSwitches(static_cast<int32_t>(mask.approach1),
                                              static_cast<int32_t>(mask.approach2),
                                              static_cast<int32_t>(mask.stopping)))
                slot.partWorldsDirty = true;
        }

        // ChangeModel(スイッチ変化によるジオメトリ差し替え)の実行時反映
        // (Car編成SceneTrainRenderSubmit.cppと同型)。下の不変スキップより前に
        // スイッチの確定(RefreshSwitchGeneration+UpdateDynamicModels)を行い、ジオメトリ差し替えが
        // 起きたフレームを必ずpartWorldsDirtyへ拾う(スキップされると
        // SetScenePartWorlds/GPU再アップロードの両方が飛ばされてしまうため)。
        if (ConfirmSessionSwitches(session)) {
            slot.sceneryGeometryDirty = true;
            slot.sceneryGpuMeshUploadFailed = false;
            slot.partWorldsDirty = true;
        }

        // entityWorldはSceneRegistry::UpdatePlacementがRevision()を進めないため
        // (=配置の変更をこの比較でしか検出できない)、スロットあたりO(1)で安いことも
        // あり毎フレーム無条件に計算する。
        // 不変スキップ: 前回計算時からentityWorldが変わっておらず、かつ全moverが
        // 静止(位相・回転が動かない)なら、パーツ行列の再計算・GPU反映は無駄になる
        // ため丸ごとスキップする。
        const ProfileClock::time_point settledStart = ProfileClock::now();
        const bool moversSettled = AllMoversSettled(session);
        settledTime += ProfileClock::now() - settledStart;
        if (!slot.partWorldsDirty &&
            slot.cachedPlacementRevision == e->placementRevision &&
            slot.cachedGraphRevision == activeGraphRevision && moversSettled) {
            ++skippedSlots;
            rs::app::ZeroSoundEffectVelocities(session.soundEffectStates);
            continue;
        }

        const ProfileClock::time_point worldMatrixStart = ProfileClock::now();
        const rs::core::Mat4 entityWorld = rs::world::ResolveWorldMatrix(e->placement, *activeGraph);
        worldMatrixTime += ProfileClock::now() - worldMatrixStart;
        if (!slot.partWorldsDirty &&
            std::memcmp(entityWorld.m, slot.cachedEntityWorld.m, sizeof(entityWorld.m)) == 0 &&
            moversSettled) {
            ++skippedSlots;
            rs::app::ZeroSoundEffectVelocities(session.soundEffectStates);
            continue;
        }
        ++lastTickRecomputedSlots_;

        // 組立表示(plugin.hasAssembly)は複数パーツから成り、各パーツの頂点はpartIndexで
        // パーツ行列を引く。可動部・Joint3D・音源位置・パーツ行列(配置の行列込み)は、
        // UpdateSessionPose で求める(ResolveDynamicPartWorldsはfloat dtを
        // 取るため、ここがdouble→float境界)。
        SessionPoseInput poseInput;
        poseInput.dt = rs::render::ToGpuFloat(dt);
        poseInput.clockSecondsOfDay = clock.SecondsOfDay();
        poseInput.entityWorld = entityWorld;
        poseInput.continuousMotion = !moversSettled;
        const ProfileClock::time_point poseStart = ProfileClock::now();
        SessionPose pose = UpdateSessionPose(session, poseInput);
        poseTime += ProfileClock::now() - poseStart;
        const std::vector<rs::core::Mat4>& dynamicWorlds = pose.dynamicWorlds;
        // 親に追従するレールの今の姿勢。駅のアニメ評価の後に更新する(RailSim II は station->Simulate の中で
        // 駅のアニメ評価に続けて番線のレールを動かす)。描画フレームの車軸解決はこの後に走る。
        UpdatePlatformParentDeltas(*activeGraph, e->guid, session.plugin.def, dynamicWorlds, entityWorld);

        // ChangeModelでジオメトリが差し替わったスロットは、パーツ行列を送る前に
        // GPUメッシュを再アップロードする(Car側SceneTrainRenderSubmit.cpp:169-192と
        // 同型。同一フレーム内でメッシュとパーツ行列が矛盾しないよう先に行う)。
        UploadSceneryGeometryIfNeeded(app, slot, i);
        UpdateSceneryPickAabb(slot, dynamicWorlds);

        const ProfileClock::time_point uploadStart = ProfileClock::now();
        app.SetScenePartWorlds(static_cast<UINT>(i), pose.partXforms, slot.residency.generation);
        if (slot.hasGpuMesh && !slot.sceneryGeometryDirty && session.HasDynamicStretchZy()) {
            const std::vector<rs::render::MeshVertex> vertices =
                rs::legacy_plugin::BuildStretchZyDynamicVertices(session.plugin, dynamicWorlds);
            if (!vertices.empty()) app.SetSceneDynamicVertices(static_cast<UINT>(i), vertices);
        }
        uploadTime += ProfileClock::now() - uploadStart;
        slot.partWorldsDirty = false;
        slot.cachedEntityWorld = entityWorld;
        slot.cachedPlacementRevision = e->placementRevision;
        slot.cachedGraphRevision = activeGraphRevision;
    }
    if (kProfileFrame) {
        const auto milliseconds = [](std::chrono::nanoseconds duration) {
            return std::chrono::duration<double, std::milli>(duration).count();
        };
        spdlog::debug("[profile-scenery] total={:.2f}ms slots={} skipped={} recomputed={} "
                     "lookup={:.2f}ms world={:.2f}ms settled={:.2f}ms pose={:.2f}ms "
                     "upload={:.2f}ms",
                     milliseconds(ProfileClock::now() - tickStart), eligibleSlots, skippedSlots,
                     lastTickRecomputedSlots_, milliseconds(lookupTime), milliseconds(worldMatrixTime),
                     milliseconds(settledTime), milliseconds(poseTime), milliseconds(uploadTime));
    }
}
void SceneSceneryRuntime::UpdateAnimations(App& app, std::vector<SceneSlotPool::Slot>& slots) {
    size_t animatedCount = 0;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].used || slots[i].isTrainCar) continue;
        SceneSlotPool::Slot& slot = slots[i];

        // 材質(スイッチ値が変化したフレームだけ)とアニメの反映は
        // ApplySessionMaterialAndAnimation(理由はその定義のコメント)。
        if (ApplySessionMaterialAndAnimation(app, app.ScenePipeline(static_cast<UINT>(i)), slot.session,
                                             slot.appliedMaterialSwitchGeneration, /*force=*/false))
            ++animatedCount;
    }
    if (animatedCount != lastAnimatedSlotCount_) { lastAnimatedSlotCount_ = animatedCount; spdlog::info("[scene] time-driven animation slots: {}", animatedCount); }
}
} // namespace rs::app
