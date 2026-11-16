//
// PluginRuntimeApplier.h - ChangeTexture/ChangeMaterial/SetAnimationの実行時反映
//
// PluginSession(1プラグイン分の読込結果+ランタイム+スイッチ表)を評価し、GPU常駐の
// マテリアル/UVを書き換える。1フレームの手順(スイッチ確定→姿勢→材質とアニメ)は
// SessionFrameStep.h がこの関数群を呼んで組み立てる。
//
#pragma once

#include <string>
#include <chrono>
#include <functional>
#include <vector>

#include "../../render/mesh/Mesh.h"
#include "../../render/pipeline/MeshPipeline.h"

namespace rs::legacy_plugin {
struct MaterialApplyResult;
struct AnimationApplyResult;
}

namespace rs::app {

class App;
struct PluginSession;

// 評価と対象選択を共有し、書き込み先だけを呼び出し側に任せる。
struct SessionRuntimeSink {
    std::function<void(bool)> beginMaterials;
    std::function<void(uint32_t, const rs::legacy_plugin::MaterialApplyResult&)> material;
    std::function<void()> beginAnimations;
    std::function<void(uint32_t, const rs::legacy_plugin::AnimationApplyResult&, bool)> animation;
};

struct SessionRuntimeTimings {
    std::chrono::nanoseconds materials{};
    std::chrono::nanoseconds animations{};
};

bool ApplySessionMaterialAndAnimation(PluginSession& session, uint64_t& appliedGeneration,
                                      const SessionRuntimeSink& sink, bool force,
                                      SessionRuntimeTimings* timings = nullptr);

// パイプライン固有のテクスチャ枠への解決は、共通評価の結果を受け取ってから行う。
bool ApplySceneRuntime(App& app, rs::render::MeshPipeline& pipeline,
                        PluginSession& session, uint64_t& appliedGeneration, bool force,
                        SessionRuntimeTimings* timings = nullptr);

struct SceneMaterialOverrides {
    std::vector<rs::render::InstanceMatOverride> values;
    struct TextureOverride {
        size_t valueIndex = 0;
        bool useTexture = false;
        std::string basePath;
        std::string normalPath;
        std::string pbrPath;
        // ChangeTexture が組み立てた法線/ORMを使ってよいか(今のEnvMap/BumpMap指定による)。
        // 偽なら、バッチが画像を張ったあとに MatData の使用フラグを落とす。
        bool allowNormal = true;
        bool allowOrm = true;
    };
    std::vector<TextureOverride> textureOverrides;
};

// GPU状態を変更せず、バッチ用の個体別マテリアル値だけを解決する。
SceneMaterialOverrides ResolveSceneMaterialOverrides(
    PluginSession& session, const std::vector<rs::render::MatData>& baseMaterialData,
    uint64_t& appliedGeneration, SceneMaterialOverrides& staticOverrides, bool force,
    SessionRuntimeTimings* timings = nullptr);

} // namespace rs::app
