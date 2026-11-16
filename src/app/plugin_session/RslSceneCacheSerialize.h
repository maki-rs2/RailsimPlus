#pragma once

#include <cstddef>

#include <nlohmann/json_fwd.hpp>

#include "LayoutSession.h"
#include "RslLayoutSerialize.h"

namespace rs::app {

nlohmann::json SceneCacheToJson(const LayoutSceneCache& cache,
                                const PluginTypeClassifier& classifier);
void RestoreSceneCache(const nlohmann::json& scene, LayoutSceneCache& cache);
size_t CountPlacementItems(const rs::world::ManualPlacementOverlay& overlay,
                           const nlohmann::json& placements);

}  // namespace rs::app
