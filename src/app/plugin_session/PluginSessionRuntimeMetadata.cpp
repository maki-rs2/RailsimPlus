#include "PluginSessionRuntimeMetadata.h"

#include <bit>
#include <algorithm>

#include "../../plugins/load/PluginLoader.h"
#include "../../plugins/load/runtime/MaterialRuntime.h"
#include "../../plugins/load/runtime/AnimationRuntime.h"
#include "../../plugins/schema/PluginSchema.h"

namespace rs::app {

std::vector<uint32_t> CollectDynamicMaterialIndices(
    const rs::legacy_plugin::LoadedPlugin& plugin,
    const std::vector<rs::legacy_plugin::PartMaterialRuntime>& materialRuntimes,
    const std::vector<rs::legacy_plugin::PartAnimationRuntime>& animationRuntimes) {
    std::vector<uint32_t> indices;
    for (size_t placementIndex = 0; placementIndex < plugin.assemblyPlacements.size(); ++placementIndex) {
        const rs::legacy_plugin::LoadedPlugin::PartPlacement& placement =
            plugin.assemblyPlacements[placementIndex];
        if (placement.partDefIndex < 0 ||
            static_cast<size_t>(placement.partDefIndex) >= plugin.def.parts.size()) {
            continue;
        }
        const size_t partIndex = static_cast<size_t>(placement.partDefIndex);
        const bool hasMaterial = partIndex < materialRuntimes.size() &&
                                 materialRuntimes[partIndex].HasDynamicMaterialChanges();
        const bool hasAnimation = partIndex < animationRuntimes.size() &&
                                  animationRuntimes[partIndex].HasEntries();
        if (!hasMaterial && !hasAnimation)
            continue;

        uint32_t materialEnd = static_cast<uint32_t>(plugin.assemblyMesh.materials.size());
        if (placementIndex + 1 < plugin.assemblyPlacements.size()) {
            materialEnd = plugin.assemblyPlacements[placementIndex + 1].matBase;
        }
        if (placement.matBase >= materialEnd) continue;

        for (const auto& submesh : plugin.assemblyMesh.submeshes) {
            if (submesh.materialIndex >= placement.matBase && submesh.materialIndex < materialEnd)
                indices.push_back(submesh.materialIndex);
        }
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

std::string ResolveGeometrySignature(const rs::legacy_plugin::LoadedPlugin& plugin) {
    std::string key;
    for (size_t partIndex = 0; partIndex < plugin.def.parts.size(); ++partIndex) {
        const rs::legacy_pi::ModelPart& part = plugin.def.parts[partIndex];
        // ChangeModel を持つパーツは、スイッチの今の値ではなく焼き込み済みの候補を使う。
        // 今の値から作ると、スイッチ変更から再焼き込みまでの間にキーだけ先に変わり、
        // 中身(メッシュ)と食い違う。
        rs::legacy_pi::ChangeModelResolveResult result;
        result.modelScale = part.baseModelScale;
        if (partIndex < plugin.dynamicModels.size() && plugin.dynamicModels[partIndex].variants) {
            result.variantIndex = plugin.dynamicModels[partIndex].currentVariantIndex;
            result.modelScale = plugin.dynamicModels[partIndex].currentScale;
        }
        const std::string& modelFile = result.variantIndex < 0
                                           ? part.baseModelFile
                                           : part.changeModels[static_cast<size_t>(result.variantIndex)].modelFile;
        const float scale = rs::legacy_plugin::EffectiveModelScale(result.modelScale);
        key += "part=" + std::to_string(partIndex);
        key += ";variant=" + std::to_string(result.variantIndex);
        key += ";model=" + std::to_string(modelFile.size()) + ":" + modelFile;
        key += ";scaleBits=";
        key += std::to_string(std::bit_cast<uint32_t>(scale));
        key.push_back(';');
    }
    return key;
}

} // namespace rs::app
