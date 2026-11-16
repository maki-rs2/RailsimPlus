#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../plugins/schema/SwitchExpr.h"

namespace rs::legacy_plugin {
struct LoadedPlugin;
class PartMaterialRuntime;
class PartAnimationRuntime;
}

namespace rs::app {

std::vector<uint32_t> CollectDynamicMaterialIndices(
    const rs::legacy_plugin::LoadedPlugin& plugin,
    const std::vector<rs::legacy_plugin::PartMaterialRuntime>& materialRuntimes,
    const std::vector<rs::legacy_plugin::PartAnimationRuntime>& animationRuntimes);

// assemblyMesh に焼き込み済みの内容を表す文字列(ChangeModel の候補と倍率)。
// 共有ジオメトリのキーに使うため、スイッチの今の値ではなく焼いた時点の記録から作る。
std::string ResolveGeometrySignature(const rs::legacy_plugin::LoadedPlugin& plugin);

} // namespace rs::app
