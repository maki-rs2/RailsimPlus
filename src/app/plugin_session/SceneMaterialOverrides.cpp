#include "PluginRuntimeApplier.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "../../plugins/load/runtime/AnimationRuntime.h"
#include "../../plugins/load/runtime/MaterialRuntime.h"
#include "PluginSession.h"

namespace rs::app {
namespace {

void SetTextureOverride(SceneMaterialOverrides& overrides, size_t valueIndex, bool useTexture,
                        const std::string& basePath, const std::string& normalPath,
                        const std::string& pbrPath, bool allowNormal = true, bool allowOrm = true) {
    const auto existing = std::find_if(
        overrides.textureOverrides.begin(), overrides.textureOverrides.end(),
        [valueIndex](const SceneMaterialOverrides::TextureOverride& candidate) {
            return candidate.valueIndex == valueIndex;
        });
    const SceneMaterialOverrides::TextureOverride value{
        valueIndex, useTexture, basePath, normalPath, pbrPath, allowNormal, allowOrm};
    if (existing == overrides.textureOverrides.end()) overrides.textureOverrides.push_back(value);
    else *existing = value;
}

} // namespace

bool ApplySessionMaterialAndAnimation(PluginSession& session, uint64_t& appliedGeneration,
                                      const SessionRuntimeSink& sink, bool force,
                                      SessionRuntimeTimings* timings) {
    using ProfileClock = std::chrono::steady_clock;
    const ProfileClock::time_point materialStart = timings ? ProfileClock::now() : ProfileClock::time_point{};
    const bool changed = force || appliedGeneration != session.SwitchGeneration();
    const SwitchEvaluationInput input = session.BuildSwitchEvaluationInput();
    const rs::legacy_pi::SwitchResolver resolve = [&input](const std::string& name) {
        return input.ResolveSwitch(name);
    };
    // アニメがテクスチャを離すフレームでも、同じ世代の材質結果へ戻してから重ねる。
    // 発動判定は各ランタイムの世代キャッシュを使う。
    if (changed || session.hasTimedAnimation) {
        if (sink.beginMaterials) sink.beginMaterials(changed);
        for (const rs::legacy_plugin::LoadedPlugin::PartPlacement& placement :
             session.plugin.assemblyPlacements) {
            if (placement.partDefIndex < 0 ||
                static_cast<size_t>(placement.partDefIndex) >= session.materialRuntimes.size()) continue;
            const rs::legacy_plugin::PartMaterialRuntime& material =
                session.materialRuntimes[static_cast<size_t>(placement.partDefIndex)];
            if (material.Empty()) continue;
            for (const rs::legacy_plugin::MaterialApplyResult& result :
                 material.Apply(resolve, session.SwitchGeneration(), session.DayAlpha())) {
                if (result.materialId < 0) continue;
                const uint32_t index = placement.matBase + static_cast<uint32_t>(result.materialId);
                if (index < session.plugin.assemblyMesh.materials.size()) sink.material(index, result);
            }
        }
    }
    const ProfileClock::time_point animationStart = timings ? ProfileClock::now() : ProfileClock::time_point{};
    if (sink.beginAnimations) sink.beginAnimations();
    if (session.hasAnimationRuntimeEntries) {
        for (const rs::legacy_plugin::LoadedPlugin::PartPlacement& placement :
             session.plugin.assemblyPlacements) {
            if (placement.partDefIndex < 0 ||
                static_cast<size_t>(placement.partDefIndex) >= session.animationRuntimes.size()) continue;
            const size_t partIndex = static_cast<size_t>(placement.partDefIndex);
            rs::legacy_plugin::PartAnimationRuntime& animation = session.animationRuntimes[partIndex];
            if (!animation.HasEntries()) continue;
            const bool managed = partIndex < session.materialRuntimes.size() &&
                                 !session.materialRuntimes[partIndex].Empty();
            animation.Update(session.animationClockSeconds, resolve, session.SwitchGeneration());
            for (const rs::legacy_plugin::AnimationApplyResult& result : animation.Results()) {
                if (result.materialId < 0) continue;
                const uint32_t index = placement.matBase + static_cast<uint32_t>(result.materialId);
                if (index < session.plugin.assemblyMesh.materials.size())
                    sink.animation(index, result, managed);
            }
        }
    }
    appliedGeneration = session.SwitchGeneration();
    if (timings) {
        timings->materials = animationStart - materialStart;
        timings->animations = ProfileClock::now() - animationStart;
    }
    return session.hasTimedAnimation;
}

SceneMaterialOverrides ResolveSceneMaterialOverrides(
    PluginSession& session, const std::vector<rs::render::MatData>& baseMaterialData,
    uint64_t& appliedGeneration, SceneMaterialOverrides& staticOverrides, bool force,
    SessionRuntimeTimings* timings) {
    SceneMaterialOverrides resolved;
    const std::vector<uint32_t>& indices = session.DynamicMaterialIndices();
    bool rebuild = false;
    const auto valueIndexOf = [&indices](uint32_t index) {
        return static_cast<size_t>(std::find(indices.begin(), indices.end(), index) - indices.begin());
    };
    SessionRuntimeSink sink;
    sink.beginMaterials = [&](bool changed) {
        rebuild = changed;
        if (!changed) return;
        staticOverrides = {};
        for (const uint32_t index : indices) {
            rs::render::InstanceMatOverride value;
            if (index < baseMaterialData.size()) value = baseMaterialData[index];
            // テクスチャ番号は出力先のプールから毎回与える。
            std::fill(std::begin(value.tex), std::end(value.tex), 0u);
            staticOverrides.values.push_back(value);
        }
    };
    sink.material = [&](uint32_t index, const rs::legacy_plugin::MaterialApplyResult& result) {
        if (!rebuild) return;
        const size_t valueIndex = valueIndexOf(index);
        if (valueIndex >= staticOverrides.values.size()) return;
        rs::render::ApplyMaterialColorToMatData(result.color, staticOverrides.values[valueIndex]);
        if (result.hasTextureOverride)
            SetTextureOverride(staticOverrides, valueIndex, !result.base.empty(), result.base,
                               result.normal, result.pbr, result.AllowsNormal(), result.AllowsOrm());
    };
    sink.beginAnimations = [&] {
        resolved = staticOverrides;
        for (size_t i = 0; i < resolved.values.size() && i < indices.size(); ++i) {
            if (indices[i] < baseMaterialData.size())
                std::memcpy(resolved.values[i].tex, baseMaterialData[indices[i]].tex,
                            sizeof(resolved.values[i].tex));
        }
    };
    sink.animation = [&](uint32_t index, const rs::legacy_plugin::AnimationApplyResult& result, bool) {
        const size_t valueIndex = valueIndexOf(index);
        if (valueIndex >= resolved.values.size()) return;
        rs::render::InstanceMatOverride& value = resolved.values[valueIndex];
        value.uvTransform0[0] = result.uv.a;
        value.uvTransform0[1] = result.uv.b;
        value.uvTransform0[2] = result.uv.c;
        value.uvTransform0[3] = result.uv.d;
        value.uvTransform1[0] = result.uv.tx;
        value.uvTransform1[1] = result.uv.ty;
        if (result.hasTexture && !result.texturePath.empty())
            SetTextureOverride(resolved, valueIndex, true, result.texturePath, "", "");
    };
    ApplySessionMaterialAndAnimation(session, appliedGeneration, sink, force, timings);
    return resolved;
}

} // namespace rs::app
