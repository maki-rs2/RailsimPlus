//
// ScenePicking.cpp
//
#include "ScenePicking.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "../../core/Epsilon.h"
#include "../../editor/tool/Editor.h"
#include "../gpu/CameraMath.h"

namespace rs::app {

namespace {

struct RayAabbHit {
    size_t index = 0;
    float entry = 0.0f;
    float exit = 0.0f;
    float sizeMetric = 0.0f;
};

// 入れ子AABBの優先順位付け用の「大きさ」指標(値が小さいほど優先=クリック意図に
// 近い的とみなす)。単純な体積(3軸の外接AABB幅の積)だと、地面/路面等の
// SurfaceプラグインのようにY方向だけ極端に薄い(高さがほぼ0の)巨大な平面が、
// 面積は広大でも体積としては小さく評価され、その上に置かれた信号機・踏切等
// 本来クリックしたい小さな物体より優先されてしまう。外接AABBの対角線長
// (3軸幅の二乗和、平方根は比較にのみ使うため省略)を使えば、薄い巨大平面は
// 広い方の2軸で対角線が伸びるため正しく「大きい」と評価される。
float WorldAabbDiagonalSq(const ScreenPickCandidate& candidate) {
    float minimum[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float maximum[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (int cornerIndex = 0; cornerIndex < 8; ++cornerIndex) {
        const float corner[4] = {
            (cornerIndex & 1) ? candidate.aabbMax[0] : candidate.aabbMin[0],
            (cornerIndex & 2) ? candidate.aabbMax[1] : candidate.aabbMin[1],
            (cornerIndex & 4) ? candidate.aabbMax[2] : candidate.aabbMin[2], 1.0f};
        float worldPoint[4];
        rs::core::TransformPoint4(candidate.worldMatrix, corner, worldPoint);
        if (std::fabs(worldPoint[3]) <= rs::core::kHomogeneousWEps) return FLT_MAX;
        for (int axis = 0; axis < 3; ++axis) {
            const float coordinate = worldPoint[axis] / worldPoint[3];
            minimum[axis] = std::min(minimum[axis], coordinate);
            maximum[axis] = std::max(maximum[axis], coordinate);
        }
    }
    float diagonalSq = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        const float extent = std::max(0.0f, maximum[axis] - minimum[axis]);
        diagonalSq += extent * extent;
    }
    return diagonalSq;
}

} // namespace

bool TransformAabbCorners(const rs::core::Mat4& worldMatrix, const float aabbMin[3],
                          const float aabbMax[3], rs::core::Vec3 outCorners[8]) {
    for (int cornerIndex = 0; cornerIndex < 8; ++cornerIndex) {
        const float corner[4] = {
            (cornerIndex & 1) ? aabbMax[0] : aabbMin[0],
            (cornerIndex & 2) ? aabbMax[1] : aabbMin[1],
            (cornerIndex & 4) ? aabbMax[2] : aabbMin[2], 1.0f};
        float worldPoint[4];
        rs::core::TransformPoint4(worldMatrix, corner, worldPoint);
        if (std::fabs(worldPoint[3]) <= rs::core::kHomogeneousWEps) return false;
        outCorners[cornerIndex] = rs::core::Vec3(worldPoint[0] / worldPoint[3], worldPoint[1] / worldPoint[3],
                                                 worldPoint[2] / worldPoint[3]);
    }
    return true;
}

bool ProjectAabbScreenRect(const rs::core::Mat4& worldMatrix, const float aabbMin[3],
                          const float aabbMax[3], const rs::core::Mat4& viewProj, unsigned int width,
                          unsigned int height, float& outMinX, float& outMinY, float& outMaxX,
                          float& outMaxY) {
    outMinX = outMinY = FLT_MAX;
    outMaxX = outMaxY = -FLT_MAX;
    bool anyVisible = false;
    for (int c = 0; c < 8; ++c) {
        const float corner[4] = {
            (c & 1) ? aabbMax[0] : aabbMin[0],
            (c & 2) ? aabbMax[1] : aabbMin[1],
            (c & 4) ? aabbMax[2] : aabbMin[2],
            1.0f,
        };
        float worldPt[4];
        rs::core::TransformPoint4(worldMatrix, corner, worldPt);
        rs::core::ProjectedPoint pp = rs::core::ProjectToScreen(
            rs::core::Vec3(worldPt[0], worldPt[1], worldPt[2]), viewProj, width, height);
        if (!pp.Visible()) continue;
        anyVisible = true;
        outMinX = std::min(outMinX, pp.x);
        outMaxX = std::max(outMaxX, pp.x);
        outMinY = std::min(outMinY, pp.y);
        outMaxY = std::max(outMaxY, pp.y);
    }
    return anyVisible;
}

bool RayIntersectAabb(const rs::core::Ray& ray, const rs::core::Mat4& worldMatrix,
                      const float aabbMin[3], const float aabbMax[3], float& outTEntry,
                      float& outTExit) {
    float worldMin[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
    float worldMax[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (int cornerIndex = 0; cornerIndex < 8; ++cornerIndex) {
        const float corner[4] = {
            (cornerIndex & 1) ? aabbMax[0] : aabbMin[0],
            (cornerIndex & 2) ? aabbMax[1] : aabbMin[1],
            (cornerIndex & 4) ? aabbMax[2] : aabbMin[2], 1.0f};
        float worldPoint[4];
        rs::core::TransformPoint4(worldMatrix, corner, worldPoint);
        if (std::fabs(worldPoint[3]) <= rs::core::kHomogeneousWEps) return false;
        for (int axis = 0; axis < 3; ++axis) {
            const float coordinate = worldPoint[axis] / worldPoint[3];
            worldMin[axis] = std::min(worldMin[axis], coordinate);
            worldMax[axis] = std::max(worldMax[axis], coordinate);
        }
    }

    float entry = -FLT_MAX;
    float exit = FLT_MAX;
    const float origin[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float direction[3] = {ray.dir.x, ray.dir.y, ray.dir.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(direction[axis]) <= rs::core::kRayParallelEps) {
            if (origin[axis] < worldMin[axis] || origin[axis] > worldMax[axis]) return false;
            continue;
        }
        float axisEntry = (worldMin[axis] - origin[axis]) / direction[axis];
        float axisExit = (worldMax[axis] - origin[axis]) / direction[axis];
        if (axisEntry > axisExit) std::swap(axisEntry, axisExit);
        entry = std::max(entry, axisEntry);
        exit = std::min(exit, axisExit);
    }
    if (entry > exit || exit < 0.0f) return false;
    outTEntry = std::max(entry, 0.0f);
    outTExit = exit;
    return true;
}

bool RayIntersectObb(const rs::core::Ray& ray, const rs::core::Mat4& worldMatrix,
                     const float aabbMin[3], const float aabbMax[3], float& outTEntry,
                     float& outTExit) {
    rs::core::Mat4 inv;
    if (!rs::core::InvertMat4(worldMatrix, inv)) return false;

    // レイをworldMatrixの逆行列でローカル空間へ変換する。row-vector慣習(p'=p*M)の
    // affine行列(回転/スケール/平行移動のみ、射影成分なし)なので、原点はw=1・方向は
    // w=0で変換すれば、変換後のtパラメータはワールド空間のtとそのまま一致する
    // (originLocal + t*dirLocal = (ray.origin + t*ray.dir)をworldMatrixの逆で
    // 引き戻したもの)。
    const float originWorld[4] = {ray.origin.x, ray.origin.y, ray.origin.z, 1.0f};
    const float dirWorld[4] = {ray.dir.x, ray.dir.y, ray.dir.z, 0.0f};
    float originLocal[4];
    float dirLocal[4];
    rs::core::TransformPoint4(inv, originWorld, originLocal);
    rs::core::TransformPoint4(inv, dirWorld, dirLocal);

    float entry = -FLT_MAX;
    float exit = FLT_MAX;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(dirLocal[axis]) <= rs::core::kRayParallelEps) {
            if (originLocal[axis] < aabbMin[axis] || originLocal[axis] > aabbMax[axis])
                return false;
            continue;
        }
        float axisEntry = (aabbMin[axis] - originLocal[axis]) / dirLocal[axis];
        float axisExit = (aabbMax[axis] - originLocal[axis]) / dirLocal[axis];
        if (axisEntry > axisExit) std::swap(axisEntry, axisExit);
        entry = std::max(entry, axisEntry);
        exit = std::min(exit, axisExit);
    }
    if (entry > exit || exit < 0.0f) return false;
    outTEntry = std::max(entry, 0.0f);
    outTExit = exit;
    return true;
}

bool PickByRay(const std::vector<ScreenPickCandidate>& candidates, const rs::core::Ray& ray,
               float maxDist, const rs::core::Mat4& viewProj, uint32_t width, uint32_t height,
               float screenX, float screenY, float pxRadius, size_t& outIndex) {
    std::vector<RayAabbHit> hits;
    for (size_t index = 0; index < candidates.size(); ++index) {
        const ScreenPickCandidate& candidate = candidates[index];
        if (!candidate.hasAabb) continue;
        // 1段目: ワールド軸並行の外接AABB(軽い、全候補に対して行う粗い絞り込み)。
        float broadEntry = 0.0f;
        float broadExit = 0.0f;
        if (!RayIntersectAabb(ray, candidate.worldMatrix, candidate.aabbMin, candidate.aabbMax,
                              broadEntry, broadExit)) continue;
        if (broadEntry > maxDist) continue;
        // 2段目: 粗い絞り込みを通過した候補だけ、ローカル空間での厳密なOBB判定へ
        // 精密化する(worldMatrixに回転が乗っている候補で、外接AABBが実モデルより
        // 大きく/ズレて誤ヒットするのを防ぐ)。逆行列を候補全件に対して毎回解くのは
        // 数千候補規模で高コストなため、ここまで絞ってから適用する。
        float entry = 0.0f;
        float exit = 0.0f;
        if (!RayIntersectObb(ray, candidate.worldMatrix, candidate.aabbMin, candidate.aabbMax,
                             entry, exit)) continue;
        if (entry > maxDist) continue;
        hits.push_back({index, entry, exit, WorldAabbDiagonalSq(candidate)});
    }
    if (!hits.empty()) {
        std::sort(hits.begin(), hits.end(), [](const RayAabbHit& left, const RayAabbHit& right) {
            return left.entry < right.entry;
        });
        const RayAabbHit& nearest = hits.front();
        const RayAabbHit* selected = &nearest;
        for (const RayAabbHit& hit : hits) {
            if (hit.entry > nearest.exit) break;
            if (hit.sizeMetric < selected->sizeMetric) selected = &hit;
        }
        outIndex = selected->index;
        return true;
    }

    // AABB無しの候補は、原点1点+画面空間pxRadius相当のワールド半径球でフォール
    // バックする(候補ごとに奥行きが異なるため、半径は候補の実際の距離から毎回
    // 算出する。maxDistを半径に流用すると、呼び出し
    // 側がAABB用の大きな上限をmaxDistへ渡したとき実質無制限のヒット判定になるため
    // 分離している)。
    bool found = false;
    float bestRayT = FLT_MAX;
    for (size_t index = 0; index < candidates.size(); ++index) {
        const ScreenPickCandidate& candidate = candidates[index];
        if (candidate.hasAabb) continue;
        const rs::core::Vec3 point(candidate.worldMatrix.m[12], candidate.worldMatrix.m[13],
                                   candidate.worldMatrix.m[14]);
        const float radius = rs::core::WorldRadiusForPixels(viewProj, width, height, screenX,
                                                            screenY, point, pxRadius);
        const float rayT = std::max(0.0f, rs::core::Vec3::Dot(point - ray.origin, ray.dir));
        const float distance = (point - (ray.origin + ray.dir * rayT)).Length();
        if (distance <= radius && rayT < bestRayT) {
            bestRayT = rayT;
            outIndex = index;
            found = true;
        }
    }
    return found;
}

bool PickByScreenRect(const std::vector<ScreenPickCandidate>& candidates, float screenX,
                     float screenY, const rs::core::Mat4& viewProj, unsigned int width, unsigned int height,
                     float pxRadius, size_t& outIndex) {
    // 1st pass: モデル実寸バウンディング矩形(pxRadiusぶん余白を持たせ、細い
    // モデルでもクリックしやすくする)。複数ヒットは面積最小を優先する。
    bool foundRect = false;
    float bestArea = FLT_MAX;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const ScreenPickCandidate& cand = candidates[i];
        if (!cand.hasAabb) continue;
        float minX, minY, maxX, maxY;
        if (!ProjectAabbScreenRect(cand.worldMatrix, cand.aabbMin, cand.aabbMax, viewProj, width,
                                  height, minX, minY, maxX, maxY))
            continue;
        minX -= pxRadius; minY -= pxRadius; maxX += pxRadius; maxY += pxRadius;
        if (screenX < minX || screenX > maxX || screenY < minY || screenY > maxY) continue;
        const float area = (maxX - minX) * (maxY - minY);
        if (area < bestArea) {
            bestArea = area;
            outIndex = i;
            foundRect = true;
        }
    }
    if (foundRect) return true;

    // 2nd pass: AABB無し(読込失敗等)の候補のみ、原点1点+固定半径にフォールバック。
    float bestDist = pxRadius;
    bool found = false;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const ScreenPickCandidate& cand = candidates[i];
        if (cand.hasAabb) continue; // AABBを持つ候補は1st passで処理済み
        const rs::core::Vec3 worldPos(cand.worldMatrix.m[12], cand.worldMatrix.m[13],
                                      cand.worldMatrix.m[14]);
        rs::core::ProjectedPoint pp =
            rs::core::ProjectToScreen(worldPos, viewProj, width, height);
        if (!pp.Visible()) continue;
        const float dx = pp.x - screenX, dy = pp.y - screenY;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d <= bestDist) {
            bestDist = d;
            outIndex = i;
            found = true;
        }
    }
    return found;
}

} // namespace rs::app
