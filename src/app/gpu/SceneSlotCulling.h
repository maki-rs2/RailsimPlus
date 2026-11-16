#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../../core/Frustum.h"
#include "../../render/mesh/Mesh.h"
#include "../../render/pipeline/MeshPipelineTypes.h"

namespace rs::app {

struct SceneSlotBounds {
    bool dirty = true;
    bool hasAabb = false;
    float aabbMin[3] = {0.0f, 0.0f, 0.0f};
    float aabbMax[3] = {0.0f, 0.0f, 0.0f};
};

// スロットに載っているメッシュのローカル範囲。カリングに要るのはこれだけなので、
// メッシュ本体の複製は持たない。
struct SceneSlotMeshBounds {
    bool hasVertices = false;
    float aabbMin[3] = {0.0f, 0.0f, 0.0f};
    float aabbMax[3] = {0.0f, 0.0f, 0.0f};
};

struct SceneSlotCullStats {
    size_t drawn = 0;
    size_t total = 0;
};

bool ComputeSlotWorldAabb(const SceneSlotMeshBounds& mesh,
                          const std::vector<rs::render::PartXform>& partWorlds,
                          float outMin[3], float outMax[3]);

// スロットが影パスの対象になる範囲にいるか。lightFrustum はカスケードのライト用視錐台。
// AABB が無いスロットは範囲を決められないので含める(切りすぎない側へ倒す)。
bool IsSlotInShadowFrustum(const SceneSlotBounds& bounds, const rs::core::Frustum& lightFrustum);

SceneSlotCullStats UpdateSceneSlotCullFlags(
    const rs::core::Frustum& frustum, const std::vector<SceneSlotMeshBounds>& sceneMeshes,
    const std::vector<std::vector<rs::render::PartXform>>& scenePartWorlds,
    std::vector<SceneSlotBounds>& bounds, std::vector<uint8_t>& outCulled);

} // namespace rs::app
