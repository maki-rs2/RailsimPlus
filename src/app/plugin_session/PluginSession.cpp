//
// PluginSession.cpp - PluginSession の読込・ランタイム構築・スイッチ解決
//
#include "PluginSession.h"


#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <iterator>
#include <numbers>
#include <regex>
#include <utility>

#include "../../platform/text/Encoding.h"
#include "../gpu/App.h"
#include "SharedStateAxes.h"
#include "../../plugins/load/runtime/StretchZyRuntime.h"

namespace rs::app {

bool PluginSession::HasDynamicStretchZy() const {
    if (plugin.stretchZyParts.empty()) return false;
    return movablePartCount > 0 || plugin.def.header.type == rs::legacy_pi::PluginType::Train ||
        std::any_of(plugin.def.parts.begin(), plugin.def.parts.end(),
                    [](const rs::legacy_pi::ModelPart& part) { return part.isZyPart; });
}

const rs::render::Mesh& PluginSession::SceneUploadMesh() {
    if (!HasDynamicStretchZy()) return plugin.assemblyMesh;
    if (!sceneUploadMesh_) {
        const rs::legacy_plugin::StretchZyVertexRange range =
            rs::legacy_plugin::FindStretchZyTailRange(plugin);
        if (range.count == 0) return plugin.assemblyMesh;
        // 全体の連続性はアップロード時だけ検査する。毎フレームは末尾しか読まない。
        for (size_t i = 0; i < range.first; ++i) {
            if (rs::legacy_plugin::FindStretchZyPartIndex(plugin, plugin.assemblyMesh.vertices[i].partIndex) >= 0)
                return plugin.assemblyMesh;
        }
        sceneUploadMesh_ = plugin.assemblyMesh;
        sceneUploadMesh_->dynamicVertexFirst = static_cast<uint32_t>(range.first);
        sceneUploadMesh_->dynamicVertexCount = static_cast<uint32_t>(range.count);
    }
    // 材質の焼き込みはジオメトリを作り直さずに変わる。
    sceneUploadMesh_->materials = plugin.assemblyMesh.materials;
    return *sceneUploadMesh_;
}

void PluginSession::LoadFromParsed(const rs::legacy_plugin::LoadedPlugin& parsed, const std::string& folderUtf8) {
    plugin = parsed;
    loadedFolderUtf8 = folderUtf8;
    BuildRuntimes();
    switches_.CollectSwitchTables(plugin.def);
    switches_.CollectCamDistBands(plugin.def);
}

void PluginSession::BuildRuntimes() {
    sceneUploadMesh_.reset();
    // 可動部ランタイムを各パーツ分ビルド
    moverRuntimes.clear();
    moverRuntimes.resize(plugin.def.parts.size());
    movablePartCount = 0;
    movableGroupCount = 0;
    hasTimeDrivenMovers = false;
    for (size_t i = 0; i < plugin.def.parts.size(); ++i) {
        moverRuntimes[i].Build(plugin.def.parts[i]);
        if (moverRuntimes[i].GroupCount() > 0) {
            ++movablePartCount;
            movableGroupCount += moverRuntimes[i].GroupCount();
            if (moverRuntimes[i].HasTimeDrivenItems()) hasTimeDrivenMovers = true;
        }
    }

    // ChangeTexture/ChangeMaterial ランタイムを各パーツ分ビルドする(テクスチャの
    // GPU 事前ロードは表示メッシュの UploadMeshToGpu が MeshPipeline::Reset() で
    // テクスチャスロットを初期化した"後"でないと上書きされてしまうため、ここでは
    // ランタイム構築のみで、GPU ロードは呼び出し側が行う)。
    soundEffectAttachments.clear();
    rs::app::BuildSoundEffectAttachments(plugin.def, loadedFolderUtf8, soundEffectAttachments);
    soundEffectStates.assign(soundEffectAttachments.size(), rs::app::SoundEffectRuntimeState{});
    headlightAttachments.clear();
    rs::app::BuildHeadlightAttachments(plugin.def, loadedFolderUtf8, headlightAttachments);
    headlightStates.assign(headlightAttachments.size(), rs::app::HeadlightRuntimeState{});

    materialRuntimes.clear();
    materialRuntimes.resize(plugin.def.parts.size());
    for (const auto& model : plugin.models) {
        if (model.partDefIndex < 0 || (size_t)model.partDefIndex >= plugin.def.parts.size())
            continue;
        const rs::legacy_pi::ModelPart& part = plugin.def.parts[model.partDefIndex];
        std::vector<std::array<std::string, 3>> resolved;
        resolved.reserve(model.resolvedChangeTextures.size());
        for (const auto& rc : model.resolvedChangeTextures)
            resolved.push_back({rc.base, rc.normal, rc.pbr});
        materialRuntimes[model.partDefIndex].Build(part, model.baseMaterials, resolved);
    }

    // SetAnimation(UV型アニメ)ランタイムを各パーツ分ビルドする。
    // plugin.resolvedAnimations(def.animationsと同じ並び)から .base だけを
    // 取り出して渡す(AnimationRuntimeはPluginLoaderへ依存させない方針)。
    animationRuntimes.clear();
    animationRuntimes.resize(plugin.def.parts.size());
    std::vector<std::vector<std::string>> resolvedAnimTex;
    resolvedAnimTex.reserve(plugin.resolvedAnimations.size());
    for (const auto& frames : plugin.resolvedAnimations) {
        std::vector<std::string> bases;
        bases.reserve(frames.size());
        for (const auto& rf : frames) bases.push_back(rf.base);
        resolvedAnimTex.push_back(std::move(bases));
    }
    // durationFrames の基準フレームレートは実際に読み込んだ定義ファイルの版で
    // 決まる(取り違えると全アニメが2倍速/半速になる)。
    const float animationFrameRateFps = rs::legacy_plugin::AnimationFrameRateFps(plugin);
    for (size_t i = 0; i < plugin.def.parts.size(); ++i)
        animationRuntimes[i].Build(plugin.def.parts[i], plugin.def.animations, resolvedAnimTex,
                                   animationFrameRateFps);

    hasTimedAnimation = false;
    hasAnimationRuntimeEntries = false;
    for (const rs::legacy_plugin::PartAnimationRuntime& runtime : animationRuntimes) {
        if (runtime.HasTimedEntries()) hasTimedAnimation = true;
        if (runtime.HasEntries()) hasAnimationRuntimeEntries = true;
    }

    dynamicMaterialIndices_ = CollectDynamicMaterialIndices(plugin, materialRuntimes, animationRuntimes);
}

void PluginSession::RefreshSwitchGeneration() {
    switches_.RefreshSwitchGeneration();
}

int32_t PluginSession::ResolveSwitch(const std::string& name) const {
    return switches_.ResolveSwitch(name);
}

SwitchEvaluationInput PluginSession::BuildSwitchEvaluationInput(
    const std::vector<int32_t>* switchOptions, std::optional<int32_t> camDist,
    std::optional<rs::world::SceneSharedSwitchValues> sharedState) const {
    return switches_.BuildEvaluationInput(plugin.def, switchOptions, camDist, sharedState);
}

void PluginSession::ApplyRoutineSwitchOverrides(const std::map<std::string, int32_t>& overrides) {
    switches_.ApplyRoutineSwitchOverrides(overrides);
}

const std::vector<int32_t>& PluginSession::CamDistBandStarts() const {
    return switches_.CamDistBandStarts();
}

bool PluginSession::CamDistQuantizationSafe() const {
    return switches_.CamDistQuantizationSafe();
}

float PluginSession::DayAlpha() const {
    return switches_.dayAlpha;
}

void PluginSession::UpdateCamDist(const float eye[3], const float* unitOrigin,
                                  float viewerFovYRadians) {
    switches_.UpdateCamDist(eye, unitOrigin, viewerFovYRadians);
}

bool PluginSession::ReferencesCamDist() const {
    return switches_.ReferencesCamDist();
}

void PluginSession::UpdateTimeSwitches(const rs::world::GameClock& clock,
                                       const rs::world::SolarState& sun, double latitudeDeg,
                                       double nightThresholdSin, bool shadowEnabled, bool envMapEnabled,
                                       bool writeCalendar) {
    switches_.UpdateTimeSwitches(clock, sun, latitudeDeg, nightThresholdSin, shadowEnabled, envMapEnabled,
                                 writeCalendar);
}

void PluginSession::UpdateTrainSwitches(double velocityKmh, double accelMhPerS, bool front,
                                        bool connectFront, bool connectRear, int serial) {
    switches_.UpdateTrainSwitches(velocityKmh, accelMhPerS, front, connectFront, connectRear, serial);
}

bool PluginSession::UpdateStationSwitches(int32_t approach1, int32_t approach2,
                                          int32_t stopping) {
    return switches_.UpdateStationSwitches(approach1, approach2, stopping);
}
void PluginSession::ApplySwitchOptionsAndBake(
    const std::vector<int32_t>& switchOptions, std::optional<int32_t> camDistBand,
    std::optional<rs::world::SceneSharedSwitchValues> sharedState) {
    ApplySwitchEvaluationAndBake(BuildSwitchEvaluationInput(&switchOptions, camDistBand, sharedState));
}

void PluginSession::ApplySwitchEvaluationAndBake(const SwitchEvaluationInput& input) {
    UserSwitches() = input.userSwitches;
    SystemSwitches() = input.systemSwitches;
    RoutineSwitchOverrides() = input.routineSwitchOverrides;
    RefreshSwitchGeneration();

    const rs::legacy_pi::SwitchResolver resolve = [&input](const std::string& name) -> int32_t {
        return input.ResolveSwitch(name);
    };

    // ChangeModel(ジオメトリ差替)を先に解決する。assemblyMesh/assemblyPlacements/
    // materialRuntimesがここで作り直される(PluginModelVariant.cpp RebuildAssembly)。
    const bool assemblyRebuilt = UpdateDynamicModels(resolve);

    // 以下のテクスチャ/色上書きループは r.hasTextureOverride が立っている場合しか
    // 書かないため、セッションを使い回して2回目以降ApplySwitchOptionsAndBakeを
    // 呼ぶと、assemblyRebuilt==falseのとき前回のスイッチ組み合わせで焼き込んだ
    // 上書きがmaterialsに残留する(=別のスイッチ値の見た目が漏れる無言の不正)。
    // これを防ぐため、上書きループへ入る前にassemblyMesh.materialsを「素の状態
    // (ChangeModel直後、まだ何も上書きしていない状態)」へ揃えてから進む。
    if (assemblyRebuilt) {
        // RebuildAssembly()でmaterialsが作り直された直後=既に素の状態。
        pristineAssemblyMaterials_ = plugin.assemblyMesh.materials;
        hasPristineAssemblyMaterials_ = true;
    } else if (!hasPristineAssemblyMaterials_) {
        // 読込直後の初回bake。RebuildAssemblyがまだ一度も走っていなければ
        // materialsは.xそのままの素の状態なので、これをスナップショットする。
        pristineAssemblyMaterials_ = plugin.assemblyMesh.materials;
        hasPristineAssemblyMaterials_ = true;
    } else if (pristineAssemblyMaterials_.size() == plugin.assemblyMesh.materials.size()) {
        // 前回焼き込んだ上書きを消し、素の状態へ戻してからこの回の上書きを行う。
        plugin.assemblyMesh.materials = pristineAssemblyMaterials_;
    } else {
        // サイズが食い違う(想定外)場合は復元できないため、現状を新たな
        // スナップショットとして扱う(安全側: 復元不能な状態で誤った値を
        // 掴ませるよりは、以後の整合性を優先する)。
        pristineAssemblyMaterials_ = plugin.assemblyMesh.materials;
    }

    // ChangeTexture/ChangeMaterial/ChangeAlphaを1回だけ解決し、GPU常駐バッファでは
    // なくassemblyMesh.materialsへ直接焼き込む(静的シーナリー用。呼び出し側は毎フレーム
    // 再解決しない前提)。PartMaterialRuntime::Apply()のcolorはbaseMaterials_の
    // コピーに色/アルファ項目だけ上書きしたものなので、非activeなテクスチャは
    // 何もせず(=既存のassemblyMesh.materials、つまり.x素の値のまま)、activeな
    // 場合のみtexturePath等を上書きする。宣言順で最後に発動した値を使う。
    for (const rs::legacy_plugin::LoadedPlugin::PartPlacement& pl : plugin.assemblyPlacements) {
        if (pl.partDefIndex < 0 || static_cast<size_t>(pl.partDefIndex) >= materialRuntimes.size())
            continue;
        rs::legacy_plugin::PartMaterialRuntime& rt = materialRuntimes[static_cast<size_t>(pl.partDefIndex)];
        if (rt.Empty()) continue;
        std::vector<rs::legacy_plugin::MaterialApplyResult> results =
            rt.Apply(resolve, SwitchGeneration(), DayAlpha());
        for (const rs::legacy_plugin::MaterialApplyResult& r : results) {
            const size_t matIdx = static_cast<size_t>(pl.matBase) + static_cast<size_t>(r.materialId);
            if (matIdx >= plugin.assemblyMesh.materials.size()) continue;
            rs::render::Material& mat = plugin.assemblyMesh.materials[matIdx];
            if (r.hasTextureOverride) {
                // 法線/ORMはEnvMap/BumpMap指定が成り立つ材質だけ使う(実行時の経路と同じ規則)。
                mat.texturePath = r.base;
                mat.normalPath = r.AllowsNormal() ? r.normal : std::string();
                mat.ormPath = r.AllowsOrm() ? r.pbr : std::string();
            }
            mat.useEnvMap = r.color.useEnvMap;
            mat.useBumpMap = r.color.useBumpMap;
            mat.forceAlphaTest = r.color.forceAlphaTest;
            mat.castShadow = r.color.castShadow;
            mat.receiveShadow = r.color.receiveShadow;
            mat.diffuse[0] = r.color.diffuse[0]; mat.diffuse[1] = r.color.diffuse[1];
            mat.diffuse[2] = r.color.diffuse[2]; mat.diffuse[3] = r.color.diffuse[3];
            mat.specular[0] = r.color.specular[0]; mat.specular[1] = r.color.specular[1];
            mat.specular[2] = r.color.specular[2];
            mat.power = r.color.power;
            mat.emissive[0] = r.color.emissive[0]; mat.emissive[1] = r.color.emissive[1];
            mat.emissive[2] = r.color.emissive[2];
        }
    }

    // texture-transformer(ShiftTexture/ScaleTexture/RotateTexture/TransformTexture、
    // 時間非依存)は読込時に1回だけ焼き込む。時間駆動のSetAnimation(UV系)は
    // 毎フレーム側(PluginRuntimeApplier)が担当するため、ここではCollectStaticUv()
    // で時間非依存のUV変換だけを取り出してassemblyMesh.materialsへ書く。
    std::vector<rs::legacy_plugin::PartAnimationRuntime::StaticUvResult> staticUvResults;
    for (const rs::legacy_plugin::LoadedPlugin::PartPlacement& pl : plugin.assemblyPlacements) {
        if (pl.partDefIndex < 0 || static_cast<size_t>(pl.partDefIndex) >= animationRuntimes.size())
            continue;
        rs::legacy_plugin::PartAnimationRuntime& art = animationRuntimes[static_cast<size_t>(pl.partDefIndex)];
        art.CollectStaticUv(resolve, SwitchGeneration(), staticUvResults);
        for (const rs::legacy_plugin::PartAnimationRuntime::StaticUvResult& r : staticUvResults) {
            const size_t matIdx = static_cast<size_t>(pl.matBase) + static_cast<size_t>(r.materialId);
            if (matIdx >= plugin.assemblyMesh.materials.size()) continue;
            rs::render::Material& mat = plugin.assemblyMesh.materials[matIdx];
            mat.uvTransform[0] = r.uv.a; mat.uvTransform[1] = r.uv.b;
            mat.uvTransform[2] = r.uv.c; mat.uvTransform[3] = r.uv.d;
            mat.uvTransform[4] = r.uv.tx; mat.uvTransform[5] = r.uv.ty;
        }
    }
}

bool PluginSession::UpdateDynamicModels(const rs::legacy_pi::SwitchResolver& resolve) {
    if (lastDynamicModelSwitchGeneration_ == SwitchGeneration()) return false;
    lastDynamicModelSwitchGeneration_ = SwitchGeneration();

    bool changed = false;
    for (size_t i = 0; i < plugin.def.parts.size(); ++i) {
        const rs::legacy_pi::ModelPart& part = plugin.def.parts[i];
        if (part.changeModels.empty()) continue;

        rs::legacy_pi::ChangeModelResolveResult r =
            rs::legacy_pi::ResolveChangeModel(part, resolve);
        if (!rs::legacy_plugin::SetModelVariantWithoutRebuild(plugin, static_cast<int>(i), r.variantIndex,
                                                              r.modelScale))
            continue;
        changed = true;

        // ジオメトリ差し替えで baseMaterials が変わったため、対応する
        // MaterialRuntime を新しい baseMaterials で作り直す(BuildRuntimes()の
        // materialRuntimes構築と同じ引数組み立て)。
        for (const auto& model : plugin.models) {
            if (model.partDefIndex != static_cast<int>(i)) continue;
            std::vector<std::array<std::string, 3>> resolved;
            resolved.reserve(model.resolvedChangeTextures.size());
            for (const auto& rc : model.resolvedChangeTextures)
                resolved.push_back({rc.base, rc.normal, rc.pbr});
            materialRuntimes[i].Rebase(part, model.baseMaterials, resolved);
            break;
        }
    }
    if (changed) {
        rs::legacy_plugin::RebuildAssembly(plugin);
        sceneUploadMesh_.reset();
        dynamicMaterialIndices_ = CollectDynamicMaterialIndices(plugin, materialRuntimes, animationRuntimes);
    }
    return changed;
}

} // namespace rs::app
