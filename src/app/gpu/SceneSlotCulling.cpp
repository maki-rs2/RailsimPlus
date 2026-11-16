#include "SceneSlotCulling.h"

#include <algorithm>
#include <cfloat>

namespace rs::app {
namespace {

void IncludePoint(const float point[3], float outMin[3], float outMax[3]) {
    for (int axis = 0; axis < 3; ++axis) {
        outMin[axis] = std::min(outMin[axis], point[axis]);
        outMax[axis] = std::max(outMax[axis], point[axis]);
    }
}

} // namespace

bool ComputeSlotWorldAabb(const SceneSlotMeshBounds& mesh,
                          const std::vector<rs::render::PartXform>& partWorlds,
                          float outMin[3], float outMax[3]) {
    if (!mesh.hasVertices) return false;
    // computeAabb() を通していないメッシュは aabbMin==aabbMax==0 のままで、そのまま
    // 使うと1点扱いになり実体があるのに切り落とされる。潰れたAABBはカリング対象外
    // (=常に描画)へ倒し、切りすぎない側へ外す。副作用として、本当に1点へ縮退した
    // メッシュもカリング対象から外れるが、描画欠けにはならないので許容する。
    if (mesh.aabbMin[0] == mesh.aabbMax[0] && mesh.aabbMin[1] == mesh.aabbMax[1] &&
        mesh.aabbMin[2] == mesh.aabbMax[2])
        return false;

    for (int axis = 0; axis < 3; ++axis) {
        outMin[axis] = FLT_MAX;
        outMax[axis] = -FLT_MAX;
    }

    const rs::render::PartXform identity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, {}};
    const size_t partCount = partWorlds.empty() ? 1 : partWorlds.size();
    // assemblyMesh はパーツオフセット未焼き込みなので、gPartWorld と同じ行列で各パーツへ
    // ローカルAABBを適用する。パーツ別AABBが無いため広めになる場合はあるが、描画欠けは起こさない。
    for (size_t part = 0; part < partCount; ++part) {
        const rs::render::PartXform& partWorld = partWorlds.empty() ? identity : partWorlds[part];
        for (int corner = 0; corner < 8; ++corner) {
            const float local[3] = {
                (corner & 1) == 0 ? mesh.aabbMin[0] : mesh.aabbMax[0],
                (corner & 2) == 0 ? mesh.aabbMin[1] : mesh.aabbMax[1],
                (corner & 4) == 0 ? mesh.aabbMin[2] : mesh.aabbMax[2],
            };
            const float localPoint[4] = {local[0], local[1], local[2], 1.0f};
            float worldPoint[4] = {};
            rs::core::TransformPoint4(partWorld.world, localPoint, worldPoint);
            const float world[3] = {worldPoint[0], worldPoint[1], worldPoint[2]};
            IncludePoint(world, outMin, outMax);
        }
    }
    return true;
}

bool IsSlotInShadowFrustum(const SceneSlotBounds& bounds, const rs::core::Frustum& lightFrustum) {
    if (!bounds.hasAabb) return true;
    return rs::core::FrustumIntersectsAabb(lightFrustum, bounds.aabbMin, bounds.aabbMax);
}

SceneSlotCullStats UpdateSceneSlotCullFlags(
    const rs::core::Frustum& frustum, const std::vector<SceneSlotMeshBounds>& sceneMeshes,
    const std::vector<std::vector<rs::render::PartXform>>& scenePartWorlds,
    std::vector<SceneSlotBounds>& bounds, std::vector<uint8_t>& outCulled) {
    if (bounds.size() < sceneMeshes.size()) bounds.resize(sceneMeshes.size());
    if (outCulled.size() < sceneMeshes.size()) outCulled.resize(sceneMeshes.size());

    const std::vector<rs::render::PartXform> emptyPartWorlds;
    SceneSlotCullStats stats{};
    for (size_t slot = 0; slot < sceneMeshes.size(); ++slot) {
        SceneSlotBounds& slotBounds = bounds[slot];
        if (slotBounds.dirty) {
            const std::vector<rs::render::PartXform>& partWorlds =
                slot < scenePartWorlds.size() ? scenePartWorlds[slot] : emptyPartWorlds;
            slotBounds.hasAabb = ComputeSlotWorldAabb(sceneMeshes[slot], partWorlds,
                                                       slotBounds.aabbMin, slotBounds.aabbMax);
            slotBounds.dirty = false;
        }
        if (!slotBounds.hasAabb) {
            outCulled[slot] = 0;
            continue;
        }

        ++stats.total;
        const bool visible = rs::core::FrustumIntersectsAabb(frustum, slotBounds.aabbMin,
                                                               slotBounds.aabbMax);
        outCulled[slot] = visible ? 0 : 1;
        if (visible) ++stats.drawn;
    }
    return stats;
}

} // namespace rs::app
