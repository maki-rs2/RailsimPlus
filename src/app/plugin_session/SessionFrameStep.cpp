#include "SessionFrameStep.h"

#include <cstring>

#include "../../plugins/schema/SwitchExpr.h"
#include "../../plugins/load/runtime/DynamicJointPlacement.h"
#include "../../plugins/load/runtime/HeadlightWorldTracker.h"
#include "../../plugins/load/runtime/SoundEffectWorldTracker.h"
#include "PluginRuntimeApplier.h"
#include "PluginSession.h"

namespace rs::app {

bool AllMoversSettled(const PluginSession& session) {
    for (const rs::legacy_plugin::PartMoverRuntime& mover : session.moverRuntimes) {
        if (!mover.Settled()) return false;
    }
    return true;
}

bool ConfirmSessionSwitches(PluginSession& session) {
    // スイッチ値が前回から変わっていなければ世代が進まず、各ランタイムは前回の発動判定を
    // 使い回す(customizerが多いプラグインで毎回全件を評価し直さないため)。
    session.RefreshSwitchGeneration();
    const SwitchEvaluationInput evaluationInput = session.BuildSwitchEvaluationInput();
    const rs::legacy_pi::SwitchResolver switchResolver = [&evaluationInput](const std::string& name) -> int32_t {
        return evaluationInput.ResolveSwitch(name);
    };
    return session.UpdateDynamicModels(switchResolver);
}

SessionPose UpdateSessionPose(PluginSession& session, const SessionPoseInput& input) {
    const SwitchEvaluationInput evaluationInput = session.BuildSwitchEvaluationInput();
    const rs::legacy_pi::SwitchResolver switchResolver = [&evaluationInput](const std::string& name) -> int32_t {
        return evaluationInput.ResolveSwitch(name);
    };
    SessionPose pose;
    // Joint3D階層をアタッチ先パーツのmover込みで解決する(可動部があればここで位相も進む)。
    // 非表示のアンカーのmoverもここで一括更新する。
    pose.dynamicWorlds = ResolveDynamicPartWorlds(
        session.plugin.def, session.moverRuntimes, switchResolver, session.SwitchGeneration(),
        input.dt, kDynamicRotateSpeedScale, input.clockSecondsOfDay);
    UpdateSoundEffectWorldPositions(session.soundEffectAttachments, pose.dynamicWorlds,
                                    input.entityWorld, input.dt, input.continuousMotion,
                                    session.soundEffectStates);
    UpdateHeadlightWorldPoses(session.headlightAttachments, pose.dynamicWorlds, input.entityWorld,
                              session.headlightStates);

    // 組立表示は複数パーツから成り、各頂点はpartIndexでパーツ行列を引く。パーツごとの行列を
    // assemblyPlacements と同じ並びで作る(配置の行列を掛けたワールド行列と、平行移動を除いた法線行列)。
    pose.partXforms.reserve(session.plugin.assemblyPlacements.size());
    for (const rs::legacy_plugin::LoadedPlugin::PartPlacement& placement :
         session.plugin.assemblyPlacements) {
        const int worldPartIndex = placement.WorldPartIndex();
        const bool hasWorld = worldPartIndex >= 0 &&
                              static_cast<size_t>(worldPartIndex) < pose.dynamicWorlds.size();
        const rs::core::Mat4 partLocal = hasWorld
                                             ? pose.dynamicWorlds[static_cast<size_t>(worldPartIndex)]
                                             : rs::core::Mat4::Identity();
        const rs::core::Mat4 world = rs::core::Mat4::Mul(partLocal, input.entityWorld);
        rs::render::PartXform xform;
        std::memcpy(xform.world, world.m, sizeof(xform.world));
        std::memcpy(xform.normalMat, world.m, sizeof(xform.normalMat));
        xform.normalMat[12] = xform.normalMat[13] = xform.normalMat[14] = 0.0f;
        pose.partXforms.push_back(xform);
    }
    return pose;
}

bool ApplySessionMaterialAndAnimation(App& app, rs::render::MeshPipeline& pipeline,
                                      PluginSession& session, uint64_t& appliedMaterialGeneration,
                                      bool force) {
    return ApplySceneRuntime(app, pipeline, session, appliedMaterialGeneration, force);
}

} // namespace rs::app
