#include "RenderQualityPreset.h"

#include <cstddef>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "../../core/ConfigJson.h"
#include "../../render/postprocess/AoQuality.h"
#include "../../render/postprocess/PostProcessPlan.h"
#include "../../render/shadow/ShadowDistance.h"
#include "../../render/shadow/ShadowQuality.h"
#include "EmbeddedRenderQualityPresets.h"

namespace rs::app {
namespace {

// 選択肢の表(keyを持つ要素の配列)から、保存用のkeyの添字を引く。無ければ読めない値として投げる。
template <typename Choices>
int ChoiceIndexOfKey(const Choices& choices, const std::string& key) {
    for (std::size_t index = 0; index < choices.size(); ++index) {
        if (key == choices[index].key) return static_cast<int>(index);
    }
    throw std::invalid_argument("選択肢にないキー: " + key);
}

RenderQualityValues ReadValues(const nlohmann::json& node) {
    RenderQualityValues values;
    values.atmosphericPerspective = node.at("atmosphericPerspective").get<bool>();
    values.heightFog = node.at("heightFog").get<bool>();
    values.shadowQualityIndex =
        ChoiceIndexOfKey(rs::render::kShadowQualityChoices, node.at("shadowQuality").get<std::string>());
    values.shadowDistanceIndex =
        ChoiceIndexOfKey(rs::render::kShadowDistanceChoices, node.at("shadowDistance").get<std::string>());
    values.sunLensFlare = node.at("sunLensFlare").get<bool>();
    values.sunWhiteout = node.at("sunWhiteout").get<bool>();
    values.miscLensFlare = node.at("miscLensFlare").get<bool>();
    values.miscParticle = node.at("miscParticle").get<bool>();
    values.bloom = node.at("bloom").get<bool>();
    values.volumetricFog = node.at("volumetricFog").get<bool>();
    values.antiAliasingIndex =
        ChoiceIndexOfKey(rs::render::kAntiAliasingChoices, node.at("antiAliasing").get<std::string>());
    values.ambientOcclusionIndex =
        ChoiceIndexOfKey(rs::render::kAoQualityChoices, node.at("ambientOcclusion").get<std::string>());
    return values;
}

RenderQualityPresets ReadPresets(const nlohmann::json& root) {
    RenderQualityPresets presets;
    for (std::size_t index = 0; index < kRenderQualityPresetKeys.size(); ++index) {
        presets.values[index] = ReadValues(root.at(std::string(kRenderQualityPresetKeys[index])));
    }
    return presets;
}

std::optional<std::size_t> PresetIndexOfKey(std::string_view key) {
    for (std::size_t index = 0; index < kRenderQualityPresetKeys.size(); ++index) {
        if (kRenderQualityPresetKeys[index] == key) return index;
    }
    return std::nullopt;
}

} // namespace

std::optional<RenderQualityPresets> RenderQualityPresets::FromJson(std::string_view json) {
    return rs::core::TryReadConfigJson<RenderQualityPresets>(json, "RenderQualityPresets", ReadPresets);
}

const RenderQualityPresets& RenderQualityPresets::Embedded() {
    static const RenderQualityPresets kPresets = rs::core::ReadEmbeddedConfig<RenderQualityPresets>(
        rs::embedded::kRenderQualityPresetsJson, rs::embedded::kRenderQualityPresetsJsonSize, "RenderQualityPresets",
        ReadPresets);
    return kPresets;
}

RenderQualityValues QualityValuesOf(const RenderSettingsViewModel& viewModel) {
    RenderQualityValues values;
    values.atmosphericPerspective = viewModel.atmosphericPerspective;
    values.heightFog = viewModel.heightFog;
    values.shadowQualityIndex = viewModel.shadowQualityIndex;
    values.shadowDistanceIndex = viewModel.shadowDistanceIndex;
    values.sunLensFlare = viewModel.sunLensFlare;
    values.sunWhiteout = viewModel.sunWhiteout;
    values.miscLensFlare = viewModel.miscLensFlare;
    values.miscParticle = viewModel.miscParticle;
    values.bloom = viewModel.bloom;
    values.volumetricFog = viewModel.volumetricFog;
    values.antiAliasingIndex = viewModel.antiAliasingIndex;
    values.ambientOcclusionIndex = viewModel.ambientOcclusionIndex;
    return values;
}

std::string_view MatchRenderQualityPreset(const RenderQualityPresets& presets, const RenderSettingsViewModel& viewModel) {
    const RenderQualityValues current = QualityValuesOf(viewModel);
    for (std::size_t index = 0; index < kRenderQualityPresetKeys.size(); ++index) {
        if (presets.values[index] == current) return kRenderQualityPresetKeys[index];
    }
    return kCustomQualityPresetKey;
}

bool ApplyRenderQualityPreset(RenderSettingsAction& action, const RenderSettingsViewModel& current,
                              const RenderQualityPresets& presets, std::string_view key) {
    const std::optional<std::size_t> preset = PresetIndexOfKey(key);
    if (!preset.has_value()) return false;
    const RenderQualityValues& target = presets.values[*preset];
    const RenderQualityValues now = QualityValuesOf(current);
    if (target.atmosphericPerspective != now.atmosphericPerspective) action.SetAtmosphericPerspective(target.atmosphericPerspective);
    if (target.heightFog != now.heightFog) action.SetHeightFog(target.heightFog);
    if (target.shadowQualityIndex != now.shadowQualityIndex) action.SetShadowQualityIndex(target.shadowQualityIndex);
    if (target.shadowDistanceIndex != now.shadowDistanceIndex) action.SetShadowDistanceIndex(target.shadowDistanceIndex);
    if (target.sunLensFlare != now.sunLensFlare) action.SetSunLensFlare(target.sunLensFlare);
    if (target.sunWhiteout != now.sunWhiteout) action.SetSunWhiteout(target.sunWhiteout);
    if (target.miscLensFlare != now.miscLensFlare) action.SetMiscLensFlare(target.miscLensFlare);
    if (target.miscParticle != now.miscParticle) action.SetMiscParticle(target.miscParticle);
    if (target.bloom != now.bloom) action.SetBloom(target.bloom);
    if (target.volumetricFog != now.volumetricFog) action.SetVolumetricFog(target.volumetricFog);
    if (target.antiAliasingIndex != now.antiAliasingIndex) action.SetAntiAliasingIndex(target.antiAliasingIndex);
    if (target.ambientOcclusionIndex != now.ambientOcclusionIndex) action.SetAmbientOcclusionIndex(target.ambientOcclusionIndex);
    return true;
}

} // namespace rs::app
