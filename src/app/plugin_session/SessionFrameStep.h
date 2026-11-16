//
// SessionFrameStep.h - 1つのプラグイン(PluginSession)を1フレーム進める手順
//
// シーンの情景スロット(SceneSceneryRuntime)が使う手順の部品。手順は3段に分かれていて、
// 呼び出し側が段の間に自分の処理を挟む。
//   1. ConfirmSessionSwitches   : スイッチの世代とChangeModelの確定。
//   2. UpdateSessionPose        : 可動部・Joint3D・音源の位置・パーツ行列。
//   3. ApplySessionMaterialAndAnimation : 材質とアニメのGPUへの反映。
// 1と2を分けるのは、情景が1と2の間で「動いていないスロットの不変スキップ」をして、
// 大量の静止した情景の姿勢計算を省くため。
//
#pragma once

#include <cstdint>
#include <vector>

#include "../../core/Math.h"
#include "../../render/pipeline/MeshPipeline.h"

namespace rs::app {

class App;
struct PluginSession;

// すべての可動部(mover)が静止しているか(位相・回転がこれ以上動かない)。
bool AllMoversSettled(const PluginSession& session);

// 可動部の時間駆動の回転に掛ける速度倍率(常に等倍)。
inline constexpr float kDynamicRotateSpeedScale = 1.0f;

// 1. スイッチの世代を確定し(RefreshSwitchGeneration)、スイッチによるジオメトリの差し替え
// (ChangeModel、UpdateDynamicModels)を行う。ジオメトリが変わったときtrueを返す
// (呼び出し側がGPUメッシュを上げ直す)。
bool ConfirmSessionSwitches(PluginSession& session);

struct SessionPoseInput {
    float dt = 0.0f;                  // このフレームの経過秒(可動部の位相を進める)
    double clockSecondsOfDay = 0.0;   // 時計の針などに渡す時刻[秒]
    rs::core::Mat4 entityWorld = rs::core::Mat4::Identity(); // 配置の行列
    bool continuousMotion = false;    // 可動部が動いているか(音源の速度を位置の差分から求めるか)
};

struct SessionPose {
    // plugin.def.parts と同じ並びの、配置の行列を掛ける前のパーツ行列(Joint3D・可動部込み)。
    std::vector<rs::core::Mat4> dynamicWorlds;
    // plugin.assemblyPlacements と同じ並びのGPU向けパーツ行列(配置の行列込み)。
    std::vector<rs::render::PartXform> partXforms;
};

// 2. 可動部とJoint3Dを今フレームの姿勢へ解決し(ResolveDynamicPartWorlds)、音源のワールド位置を
// 更新し(UpdateSoundEffectWorldPositions)、パーツ行列を作る。1フレームに1回だけ呼ぶこと
// (可動部の位相が進むため)。
SessionPose UpdateSessionPose(PluginSession& session, const SessionPoseInput& input);

// 3. 材質(ChangeTexture/ChangeMaterial/ChangeAlpha)とアニメ(SetAnimation・texture-transformer)を
// pipeline へ反映する。描画フレームごとに1回呼ぶ。
// 材質は世代変更時(または force)に流し直す。時間駆動のアニメがある場合は、毎回材質へ戻してから
// アニメを重ねる(同じ材質番号ではアニメが後勝ち)。appliedMaterialGeneration は
// 呼び出し側が持つ「前回反映した世代」で、ここで更新する。
// 戻り値: 時間駆動のアニメを毎フレーム反映した(hasTimedAnimation)か。
bool ApplySessionMaterialAndAnimation(App& app, rs::render::MeshPipeline& pipeline,
                                      PluginSession& session, uint64_t& appliedMaterialGeneration,
                                      bool force);

} // namespace rs::app
