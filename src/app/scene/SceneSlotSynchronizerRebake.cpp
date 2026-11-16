//
// SceneSlotSynchronizerRebake.cpp
//
// フォルダ一致のparkedスロットを再利用するとき(ResolveOneEntityのreusedSession経路)の、
// スイッチの焼き直しとGPU側の解放。テクスチャだけが変わるスイッチ変更では、GPUの
// メッシュ(VB/IB)を残してテクスチャの割当だけを差し替える。
#include "SceneSlotSynchronizer.h"
#include "SceneSlotSyncInternal.h"

#include <chrono>
#include <string>
#include <vector>

#include "../gpu/App.h"
#include "../plugin_session/PluginSession.h"
#include "../../render/mesh/MaterialCompare.h"
#include "../../render/pipeline/MeshPipeline.h"
#include "../../render/resource/RenderTuning.h"

#include <spdlog/spdlog.h>

namespace rs::app {

namespace {

using SceneSlot = SceneSlotPool::Slot;

// 焼き直す前の、GPUへ上げた内容の控え。焼き直し後の状態と比べて、メッシュを
// 上げ直す必要があるかを決めるのに使う。
struct PreBakeSnapshot {
    std::string geometrySignature;
    std::vector<rs::render::Material> materials;
    size_t vertexCount = 0;
    size_t indexCount = 0;
    size_t submeshCount = 0;
};

PreBakeSnapshot CapturePreBakeSnapshot(const PluginSession& session) {
    const rs::render::Mesh& mesh = session.plugin.assemblyMesh;
    PreBakeSnapshot snapshot;
    snapshot.geometrySignature = session.ResolvedGeometrySignature();
    snapshot.materials = mesh.materials;
    snapshot.vertexCount = mesh.vertices.size();
    snapshot.indexCount = mesh.indices.size();
    snapshot.submeshCount = mesh.submeshes.size();
    return snapshot;
}

// 1マテリアルぶんのテクスチャ差し替え(適用は全マテリアルの判定が通ってから)。
struct TextureRebind {
    uint32_t materialIndex = 0;
    bool hasTexture = false;
    rs::render::PreloadedTexInfo info;
};

// materialのbaseテクスチャをプールから引く。空パスは「テクスチャ無し」(hasTexture=false)
// として成功扱い。プールに無ければ(未ロード・枠切れ・プール破棄済み)falseを返す。
// ここでは新たなGPUコマンドを出さない。
bool LookupPooledTexture(const rs::render::MeshPipeline& pipeline,
                         const rs::render::Material& material, bool& hasTexture,
                         rs::render::PreloadedTexInfo& info) {
    hasTexture = !material.texturePath.empty();
    info = rs::render::PreloadedTexInfo{};
    if (!hasTexture) return true;
    return pipeline.FindPreloadedSlot(material.texturePath, material.normalPath,
                                      material.ormPath, info);
}

// 透明分類(MeshPipeline::ClassifySubmeshTransparency)がテクスチャから読む入力
// (有無・αの有無・α0の穴の有無)が同じか。他の入力はマテリアルの他フィールドが
// 同じことで揃っているので、これが同じなら分類は変わらない。
bool SameAlphaClassInputs(bool oldHasTexture, const rs::render::PreloadedTexInfo& oldInfo,
                          bool newHasTexture, const rs::render::PreloadedTexInfo& newInfo) {
    if (oldHasTexture != newHasTexture) return false;
    if (!newHasTexture) return true;
    const float minFraction = rs::render::RenderTuning::Embedded().semiAlphaMinFraction;
    return oldInfo.hasAlpha == newInfo.hasAlpha && oldInfo.hasZeroAlpha == newInfo.hasZeroAlpha &&
           (oldInfo.semiAlphaFraction >= minFraction) == (newInfo.semiAlphaFraction >= minFraction);
}

// 焼き直し後のsessionが、GPUに載っているメッシュのまま(テクスチャの割当だけ差し替えて)
// 使えるなら差し替えてtrueを返す。使えなければ何も変えずfalse。
bool TryKeepGpuMesh(App& app, size_t slotIndex, const SceneSlot& slot,
                    const PreBakeSnapshot& before, size_t& rebindCountOut) {
    const UINT gpuSlot = static_cast<UINT>(slotIndex);
    // GPUのメッシュが確かにあり、前回のアップロードが失敗や保留でないこと。
    if (!slot.hasGpuMesh || slot.sceneryGeometryDirty || slot.sceneryGpuMeshUploadFailed) return false;
    if (!app.HasSceneMesh(gpuSlot)) return false;

    const PluginSession& session = slot.session;
    const rs::render::Mesh& mesh = session.plugin.assemblyMesh;
    // ジオメトリが同じこと(署名は焼き直しで変わるChangeModelの解決結果。個数も見て、
    // 署名が表さない変化を拾う)。
    if (session.ResolvedGeometrySignature() != before.geometrySignature) return false;
    if (mesh.vertices.size() != before.vertexCount || mesh.indices.size() != before.indexCount ||
        mesh.submeshes.size() != before.submeshCount ||
        mesh.materials.size() != before.materials.size())
        return false;

    const rs::render::MeshPipeline& pipeline = app.ScenePipeline(gpuSlot);
    std::vector<TextureRebind> rebinds;
    for (size_t i = 0; i < mesh.materials.size(); ++i) {
        const rs::render::Material& oldMaterial = before.materials[i];
        const rs::render::Material& newMaterial = mesh.materials[i];
        // テクスチャパス以外(色・UV変換・透明分類に関わる指定)が1つでも違えば、
        // 描画パラメータや分類が変わるので上げ直す。
        if (!rs::render::MaterialsEqualExceptTexturePaths(oldMaterial, newMaterial)) return false;
        if (oldMaterial == newMaterial) continue;

        bool oldHasTexture = false;
        bool newHasTexture = false;
        rs::render::PreloadedTexInfo oldInfo;
        rs::render::PreloadedTexInfo newInfo;
        if (!LookupPooledTexture(pipeline, oldMaterial, oldHasTexture, oldInfo)) return false;
        if (!LookupPooledTexture(pipeline, newMaterial, newHasTexture, newInfo)) return false;
        if (!SameAlphaClassInputs(oldHasTexture, oldInfo, newHasTexture, newInfo)) return false;
        rebinds.push_back({static_cast<uint32_t>(i), newHasTexture, newInfo});
    }

    rs::render::MeshPipeline& writablePipeline = app.ScenePipeline(gpuSlot);
    for (const TextureRebind& rebind : rebinds) {
        writablePipeline.RebindMaterialBaseTexture(rebind.materialIndex, rebind.hasTexture,
                                                   rebind.info.texSlotOffset,
                                                   rebind.info.hasNormal, rebind.info.hasOrm);
    }
    rebindCountOut = rebinds.size();
    return true;
}

// このスロットのGPU側は保管前のジオメトリ・テクスチャプールを保持したままなので、
// 解放せずに新しいメッシュを上書きするとGPUがまだ使用中のリソースを書き換えることに
// なりデバイス喪失を起こす(実障害。完全一致reviveがアップロード自体を行わず、evict
// 経路が必ずここを通るため、この経路だけが抜けていた)。CPU側のsessionは保持したまま
// GPU側だけ解放する。ただしフォルダが同じ=テクスチャの顔ぶれはほぼ不変なので、残枠が
// あるうちはテクスチャプールを保持してジオメトリだけ差し替える(テクスチャ再構築が
// 切替コストの大半を占めるため。FrameRenderer::ClearSceneSlotGeometryOnly参照)。
// 残枠が尽きたら自動で全破棄へ落ちる。
void ReleaseGpuGeometry(SyncContext& ctx, const rs::world::SceneEntity& entity, size_t slotIndex) {
    if (!ctx.app.ClearSceneSlotGeometryOnly(static_cast<UINT>(slotIndex), kReuseTexSlotHeadroom)) {
        // 保持を諦めた=このスロットで枠を使い切りかけている。切替が遅くなる側へ
        // 倒れるので、頻発するならスロットのテクスチャ枠が足りていない。
        spdlog::debug("[scene] スロット{}のテクスチャ枠が不足のため再構築します(folder={})",
                      slotIndex, entity.folderUtf8);
    }
}

} // namespace

// カメラや時刻の供給タイミングを変えず、セッションの現在値を焼き込みへ引き渡す。
void BakeSwitchOptionsWithProfile(SyncContext& ctx, SceneSlotPool::Slot& slot,
                                  const rs::world::SceneEntity& entity) {
    const SwitchEvaluationInput input =
        slot.session.BuildSwitchEvaluationInput(&entity.switchOptions);
    if (!ctx.profileEnabled) {
        slot.session.ApplySwitchEvaluationAndBake(input);
        return;
    }
    using ProfClock = std::chrono::steady_clock;
    const ProfClock::time_point tBake0 = ProfClock::now();
    slot.session.ApplySwitchEvaluationAndBake(input);
    ctx.profile.bakeMs +=
        std::chrono::duration<double, std::milli>(ProfClock::now() - tBake0).count();
    ++ctx.profile.bakeCount;
}

bool RebakeReusedSlot(SyncContext& ctx, const rs::world::SceneEntity& entity, size_t slotIndex) {
    SceneSlot& slot = ctx.slots[slotIndex];

    if (entity.category != rs::world::SceneEntityCategory::Scenery) {
        // 車両はバッチ描画との兼ね合いがあるため、解放してから焼き直す。
        ReleaseGpuGeometry(ctx, entity, slotIndex);
        BakeSwitchOptionsWithProfile(ctx, slot, entity);
        return false;
    }

    // 焼き直す前の内容を控え、焼き直した結果と見比べてから解放するか決める。
    const PreBakeSnapshot before = CapturePreBakeSnapshot(slot.session);
    BakeSwitchOptionsWithProfile(ctx, slot, entity);
    size_t rebindCount = 0;
    if (TryKeepGpuMesh(ctx.app, slotIndex, slot, before, rebindCount)) {
        // 解放(ClearSceneSlotInternal)を通らないので、parkで隠した状態を戻す。
        ctx.app.SetSceneSlotVisible(static_cast<UINT>(slotIndex), true);
        ctx.app.SetSceneSlotFadeAlpha(static_cast<UINT>(slotIndex), 1.0f);
        if (ctx.profileEnabled) {
            ++ctx.profile.meshSkipCount;
            ctx.profile.textureRebindCount += static_cast<int>(rebindCount);
        }
        return true;
    }
    ReleaseGpuGeometry(ctx, entity, slotIndex);
    return false;
}

} // namespace rs::app
