#include "ViewportPierBatchPlan.h"
#include "../scene/ParsedPluginCache.h"

#include <algorithm>
#include <cmath>

namespace rs::app {

PierBatchPlan ComputePierBatchPlan(const std::string& folderUtf8, const TrackPluginEntry* entry,
                                    const rs::core::Mat4& world) {
    PierBatchPlan plan;
    if (entry == nullptr) return plan;  // skip=true

    const float heightM = world.m[13];
    // RailSim II の「低い盛土区間では橋脚を作らない」閾値(buildMinAlt未満はスキップ)。
    if (heightM < entry->def.pier.buildMinAlt) return plan;  // skip=true

    plan.roundedHeight = std::max(0.0f, std::round(heightM / 0.05f) * 0.05f);
    plan.key = MakePluginMeshKey(MakePluginSourceKey(folderUtf8, "") + "|" + std::to_string(plan.roundedHeight));
    plan.skip = false;
    return plan;
}

void ZeroPierWorldY(rs::core::Mat4& world) { world.m[13] = 0.0f; }

} // namespace rs::app
