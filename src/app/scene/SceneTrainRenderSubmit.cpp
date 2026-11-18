#include "../../render/RenderPrecision.h"
#include "../gpu/App.h"
#include "SceneTrainController.h"

#include "../../platform/EnvironmentVariable.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include <spdlog/spdlog.h>

#include "../../plugins/load/runtime/DynamicJointPlacement.h"
#include "../../plugins/load/runtime/HeadlightWorldTracker.h"
#include "../../plugins/load/runtime/SoundEffectWorldTracker.h"
#include "../../plugins/load/runtime/StretchZyRuntime.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../plugin_session/PluginRuntimeApplier.h"
#include "../plugin_session/SessionFrameStep.h"
#include "SceneTrainWheelSound.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {
namespace {
// 計測用: バッチ成功/フォールバックの内訳をRS_PROFILE_FRAME有効時のみ出力する。
const bool kProfileFrame = rs::platform::ReadEnvironmentVariable("RS_PROFILE_FRAME") != nullptr;

//車体の行列を基準に、描画と同じパーツ行列から赤枠とピッキング用の箱を作る
//車軸は回転で箱が揺れるので対象外にする(車体や台車の内側に収まる)
void UpdateCarPickBox(SceneSlotPool::Slot& slot) {
    using AnchorKind = rs::legacy_plugin::TrainVehicleTemplate::PartRenderInfo::AnchorKind;
    SceneSlotPool::CarRenderSnapshot& car = slot.carSnapshot;
    const std::vector<rs::legacy_plugin::LoadedPlugin::PartPlacement>& placements =
        slot.session.plugin.assemblyPlacements;
    if (car.carPartBoundsDirty || car.carPartBounds.size() != placements.size()) {
        car.carPartBounds = ComputePartLocalBounds(slot.session.plugin.assemblyMesh.vertices,
                                                   placements.size());
        car.carPartBoundsDirty = false;
    }
    const rs::legacy_plugin::TrainVehicleTemplate& tmpl = car.trainTemplate;
    //基準は末尾のBody(通常は車体)。無い縮退プラグインはheadの行列
    rs::core::Mat4 frame = car.lastCarWorld;
    if (!tmpl.bodyPartIndex.empty()) {
        const size_t bodyDefIndex = static_cast<size_t>(tmpl.bodyPartIndex.back());
        if (bodyDefIndex < car.carDefPartWorlds.size()) frame = car.carDefPartWorlds[bodyDefIndex];
    }
    std::vector<rs::core::Mat4> worlds(placements.size(), rs::core::Mat4::Identity());
    std::vector<bool> included(placements.size(), false);
    const size_t count = std::min(placements.size(), car.carPartWorlds.size());
    for (size_t i = 0; i < count; ++i) {
        std::memcpy(worlds[i].m, car.carPartWorlds[i].world, sizeof(worlds[i].m));
        const int defIndex = placements[i].WorldPartIndex();
        included[i] = defIndex >= 0 && static_cast<size_t>(defIndex) < tmpl.partRender.size() &&
                      tmpl.partRender[static_cast<size_t>(defIndex)].anchorKind != AnchorKind::Axle;
    }
    const TrainPickBox box = BuildTrainPickBox(car.carPartBounds, worlds, included, frame);
    car.carPickFrame = frame;
    car.carPickValid = box.valid;
    std::memcpy(car.carPickAabbMin, box.aabbMin, sizeof(car.carPickAabbMin));
    std::memcpy(car.carPickAabbMax, box.aabbMax, sizeof(car.carPickAabbMax));
}
}

void SceneTrainController::SubmitRenderSnapshot(
    App& app, const rs::world::TrackGraph& activeGraph, const float eye[3],
    const rs::world::GameClock& clock, const rs::world::SolarState& sun, double latitudeDeg,
    double nightThresholdSin, std::vector<SceneSlotPool::Slot>& slots, float viewerFovYRadians,
    const WheelSoundFrame& wheelFrame, std::vector<rs::sim::WheelSoundEvent>& wheelEvents,
    const rs::legacy_plugin::TrackingTargetTable* trackingTargets) {
    using ProfileClock = std::chrono::steady_clock;
    lastRenderSubmitStats_ = {};
    const ProfileClock::time_point submitStart = ProfileClock::now();
    SubmitFrame frame;
    frame.app = &app;
    frame.activeGraph = &activeGraph;
    frame.eye = eye;
    frame.clock = &clock;
    frame.sun = &sun;
    frame.latitudeDeg = latitudeDeg;
    frame.nightThresholdSin = nightThresholdSin;
    frame.viewerFovYRadians = viewerFovYRadians;
    frame.wheelFrame = &wheelFrame;
    frame.wheelEvents = &wheelEvents;
    frame.trackingTargets = trackingTargets;

    batching_.BeginFrame(app, slots);
    for (size_t slotIndex = 0; slotIndex < slots.size(); ++slotIndex) {
        Slot& slot = slots[slotIndex];
        if (!slot.used || !slot.isTrainCar) continue;
        SubmitCarSlot(frame, slotIndex, slot);
    }

    batching_.EndFrame(app);

    const SceneTrainBatching::FrameStats& batchStats = batching_.CurrentFrameStats();
    const auto milliseconds = [](std::chrono::nanoseconds duration) {
        return std::chrono::duration<double, std::milli>(duration).count();
    };
    lastRenderSubmitStats_.submitMs = std::chrono::duration<double, std::milli>(
        ProfileClock::now() - submitStart).count();
    lastRenderSubmitStats_.batchMs = milliseconds(batchStats.indexTime + batchStats.eligibilityTime +
                                                   batchStats.staticOverrideResolveTime +
                                                   batchStats.animationOverlayTime +
                                                   batchStats.textureOverrideTime +
                                                   batchStats.addInstanceTime +
                                                   batchStats.ensureBatchTime);
    if (kProfileFrame) {
        spdlog::debug("[profile-train-submit] batched={} fallback={} keys={}/{}"
                     "(content={} folders={} hist=1:{}/2:{}/3-4:{}/5+:{}) modelSwaps={} "
                     "geomUploads={} runtime={:.2f}ms "
                     "partWorld={:.2f}ms batch={:.2f}ms(texture={:.2f}ms addInstance={:.2f}ms "
                     "ensureBatch={:.2f}ms/{}calls/{}fail) fallback={:.2f}ms(material={:.2f}ms "
                     "anim={:.2f}ms setPartWorlds={:.2f}ms) submit={:.2f}ms",
                     frame.batchedCount, frame.fallbackCount, batchStats.distinctKeys, batchStats.indexedSlots,
                     batchStats.distinctContentKeys, batchStats.distinctFolders,
                     batchStats.keysWithCount1,
                     batchStats.keysWithCount2, batchStats.keysWithCount3to4,
                     batchStats.keysWithCount5plus,
                     batchStats.batchKeyRefreshes,
                     frame.geometryUploadCount, lastRenderSubmitStats_.runtimeMs,
                     lastRenderSubmitStats_.partWorldMs, lastRenderSubmitStats_.batchMs,
                     milliseconds(batchStats.textureOverrideTime),
                     milliseconds(batchStats.addInstanceTime),
                     milliseconds(batchStats.ensureBatchTime), batchStats.ensureBatchCalls,
                     batchStats.ensureBatchFailures, lastRenderSubmitStats_.fallbackMs,
                     frame.fallbackMaterialMs, frame.fallbackAnimationMs, frame.fallbackPartWorldsMs,
                     lastRenderSubmitStats_.submitMs);
    }
}

void SceneTrainController::SubmitCarSlot(SubmitFrame& frame, size_t slotIndex, Slot& slot) {
    using ProfileClock = std::chrono::steady_clock;
    App& app = *frame.app;
    const rs::world::TrackGraph& activeGraph = *frame.activeGraph;
    if (!slot.carSnapshot.carSnapshotValid) {
        slot.carSnapshot.wheelOdometerValid = false;
        app.SetSceneSlotFadeAlpha(static_cast<UINT>(slotIndex), 1.0f);
        app.SetSceneSlotVisible(static_cast<UINT>(slotIndex), false);
        return;
    }

    // Scene切替(SwitchLayoutScene)はeditor.graphを丸ごと差し替えるが、trainの
    // スナップショットは固定tick(SceneTrainController::Tick)でしか更新されない。
    // 切替直後のフレームでtickが1回も回らない(gameTicks==0)と、ここへ旧Sceneのedge Guidを持ったまま到達し、
    // SolvePartWorlds内のgraph.edges.at()が例外を投げてプロセスごと落ちる。
    // 参照先edgeが現在のgraphに無いスナップショットは無効化して描画を飛ばす
    // (次のtickで正しいSceneのposeが入り直る)。
    if (!activeGraph.edges.contains(slot.carSnapshot.carSnapshotHead.edge)) {
        slot.carSnapshot.carSnapshotValid = false;
        slot.carSnapshot.carVisible = false;
        slot.carSnapshot.wheelOdometerValid = false;
        app.SetSceneSlotVisible(static_cast<UINT>(slotIndex), false);
        return;
    }

    PluginSession& session = slot.session;
    const ProfileClock::time_point runtimeStart = ProfileClock::now();
    float unitOrigin[3] = {slot.carSnapshot.lastCarWorld.m[12], slot.carSnapshot.lastCarWorld.m[13],
                           slot.carSnapshot.lastCarWorld.m[14]};
    session.UpdateCamDist(frame.eye, unitOrigin, frame.viewerFovYRadians);
    session.UpdateTimeSwitches(*frame.clock, *frame.sun, frame.latitudeDeg, frame.nightThresholdSin,
                               app.ShadowsEnabled(), app.EnvMapEnabled());
    const double velocityKmh = runtime_.GroupCurrentSpeedKmh(slot.carSnapshot.carSnapshotTrainId);
    const double accelMhPerS = runtime_.GroupCurrentAccelMhPerS(slot.carSnapshot.carSnapshotTrainId);
    const bool front = slot.carSnapshot.carSnapshotReversed !=
                       runtime_.GroupLastTravelReversed(slot.carSnapshot.carSnapshotTrainId);
    session.UpdateTrainSwitches(velocityKmh, accelMhPerS, front,
                                slot.carSnapshot.carSnapshotConnectFront,
                                slot.carSnapshot.carSnapshotConnectRear,
                                static_cast<int>(rs::world::GuidHash{}(slot.carSnapshot.carSnapshotTrainId) & 0x7fffffffU));
    session.ApplyRoutineSwitchOverrides(slot.carSnapshot.carSnapshotRoutineSwitchOverrides);
    session.RefreshSwitchGeneration();
    const SwitchEvaluationInput input = session.BuildSwitchEvaluationInput();
    const rs::legacy_pi::SwitchResolver switchResolver =
        [&input](const std::string& name) -> int32_t {
            return input.ResolveSwitch(name);
        };
    if (session.UpdateDynamicModels(switchResolver)) {
        slot.carSnapshot.carGeometryDirty = true;
        slot.carSnapshot.carPartBoundsDirty = true;
        slot.carSnapshot.carGpuMeshUploadFailed = false;
        batching_.RefreshSlotBatchKey(slotIndex, slot);
    }

    const ProfileClock::time_point partWorldStart = ProfileClock::now();
    const std::vector<rs::core::Mat4> solved = rs::legacy_plugin::SolvePartWorlds(
        activeGraph, slot.carSnapshot.carSnapshotHead, slot.carSnapshot.trainTemplate,
        slot.carSnapshot.carSnapshotDeltaSeconds, slot.carSnapshot.carSnapshotDistanceMeters, &slot.carSnapshot.axleSpin,
        &slot.carSnapshot.tiltState, 1.0, 1.0, nullptr, slot.carSnapshot.carSnapshotReversed,
        runtime_.GroupOccupiedPath(slot.carSnapshot.carSnapshotTrainId),
        slot.carSnapshot.carSnapshotPathOffsetM, &slot.carSnapshot.carAxleContacts);
    const double solveMs =
        std::chrono::duration<double, std::milli>(ProfileClock::now() - partWorldStart).count();
    rs::legacy_plugin::TrackingMoveContext tracking;
    tracking.targets = frame.trackingTargets;
    tracking.placementToWorld = [&](size_t partIndex, rs::core::Mat4& out) {
        return rs::legacy_plugin::FindPartPlacementToWorld(
            session.plugin.def, slot.carSnapshot.trainTemplate, solved, partIndex, out);
    };
    // 可動部を持つ車両は、ResolveDynamicPartWorlds で全パーツを
    // 解き直す(親の可動部が Joint3D の子へ伝わり、ZY系も今の姿勢で解ける)。
    std::vector<rs::core::Mat4> dynamicWorlds;
    if (session.movablePartCount > 0) {
        dynamicWorlds = rs::app::ResolveDynamicPartWorlds(
            session.plugin.def, session.moverRuntimes, switchResolver, session.SwitchGeneration(),
            rs::render::ToGpuFloat(slot.carSnapshot.carSnapshotDeltaSeconds), kDynamicRotateSpeedScale,
            frame.clock->SecondsOfDay(), &tracking);
    }
    if (session.hasTimedAnimation) {
        session.animationClockSeconds += slot.carSnapshot.carSnapshotDeltaSeconds;
    }
    lastRenderSubmitStats_.runtimeMs +=
        std::chrono::duration<double, std::milli>(ProfileClock::now() - runtimeStart).count() - solveMs;

    const ProfileClock::time_point composeWorldStart = ProfileClock::now();
    CollectCarWheelSounds(slot, activeGraph, runtime_.GroupOdometerM(slot.carSnapshot.carSnapshotTrainId),
                          *frame.wheelFrame, *frame.wheelEvents);
    // SolvePartWorldsの結果はpartDefIndex添字のワールド行列なので、可動部の変位を
    // 合成すれば情景と同じSoundEffect追跡へ渡せる(情景側と違いentityWorldの合成は不要)。
    // 可動部の変位を合成した後の行列を渡すこと。音源を遠方へ置いておき、
    // 条件成立時にStaticMoveで手元へ引き寄せることで可聴/不可聴を切り替える
    // 書き方があり、合成前の行列だと音源が常に遠方のままカリングされ続ける。
    const std::vector<rs::core::Mat4> partWorlds =
        dynamicWorlds.empty()
            ? solved
            : rs::legacy_plugin::ComposeDynamicPartWorlds(
                  session.plugin.def, slot.carSnapshot.trainTemplate, solved, dynamicWorlds);
    // カブ視点のFrontCabin/TailCabinピボット解決(SceneTrainController::
    // ResolveCabinPivot)向けに、def.parts添字のワールド行列を保持する。
    // RailSim IIのCObjectJoint3D::GetPos等は
    // 取り付け先の GetMatrix() を使い、その行列には SetPostureFreeObject で可動部が
    // 掛かっているので、可動部を合成した後の値を使う。
    slot.carSnapshot.carDefPartWorlds = partWorlds;
    rs::app::UpdateSoundEffectWorldPositions(session.soundEffectAttachments, partWorlds,
                                             rs::core::Mat4::Identity(), 0.0f, false,
                                             session.soundEffectStates);
    rs::app::UpdateHeadlightWorldPoses(session.headlightAttachments, partWorlds,
                                       rs::core::Mat4::Identity(), session.headlightStates);
    rs::app::UpdateParticleWorldPoses(session.particleAttachments, partWorlds,
                                      rs::core::Mat4::Identity(), session.particleStates);
    // 音源速度は「符号付き走行速度×軌道接線」で解析的に求める。位置差分は使わない:
    // この関数は描画フレームごとに1回走るのに対しcarSnapshotDeltaSecondsは
    // 1tick分であり、分子と分母が別の区間を測るので値が振動する。
    // 音源は車体に固定されているため編成内で速度は共通でよい(可動部自身の
    // 動きぶんは無視する)。
    const rs::world::TrackFrame headFrame = rs::world::EvaluateAt(
        activeGraph.edges.at(slot.carSnapshot.carSnapshotHead.edge), slot.carSnapshot.carSnapshotHead.s,
        slot.carSnapshot.carSnapshotHead.direction);
    rs::app::SetSoundEffectVelocities(
        session.soundEffectStates,
        rs::app::ComputeTrainSoundVelocity(headFrame.tangent, velocityKmh));
    slot.carSnapshot.carPartWorlds.clear();
    slot.carSnapshot.carPartWorlds.reserve(session.plugin.assemblyPlacements.size());
    for (const rs::legacy_plugin::LoadedPlugin::PartPlacement& placement :
         session.plugin.assemblyPlacements) {
        rs::core::Mat4 world = rs::core::Mat4::Identity();
        const int worldPartIndex = placement.WorldPartIndex();
        if (worldPartIndex >= 0 &&
            static_cast<size_t>(worldPartIndex) < partWorlds.size()) {
            world = partWorlds[static_cast<size_t>(worldPartIndex)];
        } else if (worldPartIndex == -1 && rs::legacy_plugin::FindStretchZyPartIndex(
                       session.plugin, static_cast<uint32_t>(slot.carSnapshot.carPartWorlds.size())) >= 0) {
            world = slot.carSnapshot.lastCarWorld;
        }
        rs::render::PartXform partTransform;
        std::memcpy(partTransform.world, world.m, sizeof(partTransform.world));
        std::memcpy(partTransform.normalMat, world.m, sizeof(partTransform.normalMat));
        partTransform.normalMat[12] = partTransform.normalMat[13] =
            partTransform.normalMat[14] = 0.0f;
        slot.carSnapshot.carPartWorlds.push_back(partTransform);
    }
    UpdateCarPickBox(slot);
    slot.carSnapshot.carSnapshotDeltaSeconds = 0.0;
    slot.carSnapshot.carSnapshotDistanceMeters = 0.0;
    lastRenderSubmitStats_.partWorldMs +=
        solveMs + std::chrono::duration<double, std::milli>(ProfileClock::now() - composeWorldStart).count();

    const bool addedToBatch = batching_.TryAddInstance(app, slotIndex, slot, slot.carSnapshot.carPartWorlds);
    if (addedToBatch) {
        ++frame.batchedCount;
        return;
    }
    ++frame.fallbackCount;
    SubmitFallbackSlot(frame, slotIndex, slot);
}

void SceneTrainController::SubmitFallbackSlot(SubmitFrame& frame, size_t slotIndex, Slot& slot) {
    using ProfileClock = std::chrono::steady_clock;
    App& app = *frame.app;
    const ProfileClock::time_point fallbackStart = ProfileClock::now();
    app.SetSceneSlotFadeAlpha(static_cast<UINT>(slotIndex), slot.carSnapshot.carFadeAlpha);
    app.SetSceneSlotVisible(static_cast<UINT>(slotIndex), true);
    const bool forceMaterial = slot.carSnapshot.carGeometryDirty || !slot.hasGpuMesh;
    if (!slot.carSnapshot.carGpuMeshUploadFailed && (slot.carSnapshot.carGeometryDirty || !slot.hasGpuMesh)) {
        // assemblyMeshが空(LOD非表示等でChangeModel変更のたびにcarGeometryDirty
        // だけが立つケース)は、何度アップロードを試みても必ず失敗する。ChangeModel
        // 側は差分検知(PluginModelVariant.cpp:111)があるため呼ばれること自体は
        // 正常だが、ここで毎回UploadSceneMeshToGpuまで進めると空メッシュ警告
        // (MeshUpload.cpp)が無駄に連発するので、事前に弾いて恒久失敗として
        // 記録する(バッチ側SceneTrainBatching.cppの同種早期
        // リターンとロジックを揃えている)。
        if (slot.session.plugin.assemblyMesh.vertices.empty() ||
            slot.session.plugin.assemblyMesh.indices.empty()) {
            slot.carSnapshot.carGpuMeshUploadFailed = true;
        } else {
            const std::string geometryKey = MakePluginMeshKey(slot.residency.contentKey) + "|" +
                                            slot.session.ResolvedGeometrySignature();
            ++frame.geometryUploadCount;
            if (app.UploadSceneMeshToGpu(static_cast<UINT>(slotIndex), geometryKey,
                                         slot.session.SceneUploadMesh())) {
                slot.hasGpuMesh = true;
                slot.carSnapshot.carGeometryDirty = false;
            } else {
                slot.carSnapshot.carGpuMeshUploadFailed = true;
            }
        }
    }
    SessionRuntimeTimings timings;
    ApplySceneRuntime(app, app.ScenePipeline(static_cast<UINT>(slotIndex)), slot.session,
                      slot.appliedMaterialSwitchGeneration, forceMaterial, &timings);
    frame.fallbackMaterialMs +=
        std::chrono::duration<double, std::milli>(timings.materials).count();
    frame.fallbackAnimationMs +=
        std::chrono::duration<double, std::milli>(timings.animations).count();
    const ProfileClock::time_point setPartWorldsStart = ProfileClock::now();
    app.SetScenePartWorlds(static_cast<UINT>(slotIndex), slot.carSnapshot.carPartWorlds,
                          slot.residency.generation);
    if (slot.hasGpuMesh && !slot.carSnapshot.carGeometryDirty && slot.session.HasDynamicStretchZy()) {
        const std::vector<rs::render::MeshVertex> vertices =
            rs::legacy_plugin::BuildStretchZyDynamicVertices(
                slot.session.plugin, slot.carSnapshot.carDefPartWorlds, slot.carSnapshot.lastCarWorld);
        if (!vertices.empty()) app.SetSceneDynamicVertices(static_cast<UINT>(slotIndex), vertices);
    }
    frame.fallbackPartWorldsMs +=
        std::chrono::duration<double, std::milli>(ProfileClock::now() - setPartWorldsStart).count();
    lastRenderSubmitStats_.fallbackMs +=
        std::chrono::duration<double, std::milli>(ProfileClock::now() - fallbackStart).count();
}

} // namespace rs::app
