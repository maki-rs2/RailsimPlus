//
// PierMeshBuilder.cpp - メッシュ合成(AppendOffsetMesh)、曲線に沿った反復複製(BuildIntervalMesh)と、その反復モデルの読込(LoadIntervalModels)。
// Rail断面の押し出し(BuildRailMesh/BuildRailMeshAlongSpline)はRailMeshBuilder.cpp、
// Pierシャフトの押し出し(BuildPierShaftMesh)はPierShaftMeshBuilder.cpp。
//
#include "../../core/Epsilon.h"
#include "RailMeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "PluginPaths.h"
#include "../../render/resource/XLoader.h"
#include "../../world/alignment/SplineSampleBracket.h"
#include "../schema/PluginSchema.h"

namespace rs::legacy_plugin {

std::vector<LoadedIntervalModel> LoadIntervalModels(const rs::legacy_pi::PluginDefinition& def,
                                                    const std::wstring& baseDir) {
    std::vector<LoadedIntervalModel> models;
    for (const rs::legacy_pi::RailInterval& interval : def.intervals) {
        const std::wstring modelPath = ResolvePath(baseDir, interval.modelFile);
        if (!FileExists(modelPath)) continue;
        rs::legacy_x::LoadResult loaded = rs::legacy_x::LoadXFileAssimp(modelPath);
        if (!loaded.ok) continue;
        models.push_back(LoadedIntervalModel{&interval, std::move(loaded.mesh)});
    }
    return models;
}

// mesh の全頂点マテリアルインデックスを matBase だけオフセットして out へ追記し、
// 頂点座標に scale を掛け (0,offsetY,0) 平行移動してマージする。
// Pier の Base/Head/Joint モデルをシャフトへ積み上げ合成するための共通処理。
void AppendOffsetMesh(rs::render::Mesh& out, const rs::render::Mesh& src,
                     float scale, float offsetY) {
    uint32_t vBase = static_cast<uint32_t>(out.vertices.size());
    uint32_t matBase = static_cast<uint32_t>(out.materials.size());
    for (const auto& v : src.vertices) {
        rs::render::MeshVertex mv = v;
        mv.pos[0] = v.pos[0] * scale;
        mv.pos[1] = v.pos[1] * scale + offsetY;
        mv.pos[2] = v.pos[2] * scale;
        out.vertices.push_back(mv);
    }
    for (const auto& mat : src.materials) out.materials.push_back(mat);
    for (const auto& sm : src.submeshes) {
        rs::render::SubMesh osm = sm;
        osm.materialIndex += matBase;
        osm.indexOffset = static_cast<uint32_t>(out.indices.size());
        out.submeshes.push_back(osm);
        for (uint32_t k = 0; k < sm.indexCount; ++k)
            out.indices.push_back(src.indices[sm.indexOffset + k] + vBase);
    }
}

// 1つのモデルメッシュを曲線(curveRadius, 0=直線)に沿って interval[m] 間隔で
// length[m] 分だけ反復複製し1メッシュにマージする(まくらぎ等)。scale は頂点座標に
// 適用。s=0..length に count 個並べ、押し出しレールと始点を揃える。
// カント(横断傾斜)は BuildRailMesh 円弧ブランチと同じ規約(断面ローカルX-Y平面での
// 剛体回転、内側レール頭頂軸=pivotX=sign(R)*gauge/2, pivotY=0)で適用する。
rs::render::Mesh BuildIntervalMesh(const rs::render::Mesh& src, float scale,
                                   float interval, float length,
                                   float curveRadius, float cantDeg, float gauge,
                                   bool ignoreCant) {
    rs::render::Mesh out;
    out.materials = src.materials;
    const bool straight = std::fabs(curveRadius) < rs::core::kStraightRadiusEpsM;
    const float cantRad = ignoreCant ? 0.0f : (cantDeg * std::numbers::pi_v<float> / 180.0f);
    const float sideSign = (curveRadius >= 0.0f) ? 1.0f : -1.0f;
    const float rollPhi = -sideSign * cantRad;
    const float cRoll = std::cos(rollPhi), sRoll = std::sin(rollPhi);
    const float pivotX = sideSign * gauge * 0.5f;
    const float pivotY = 0.0f;

    int count = (interval > rs::core::kMinRepeatIntervalM) ? static_cast<int>(length / interval) + 1 : 1;
    for (int r = 0; r < count; ++r) {
        float s = r * interval;
        float theta = 0.0f, cx = 0.0f, cz = s;
        if (!straight) {
            theta = s / curveRadius;
            cx = curveRadius * (1.0f - std::cos(theta));
            cz = curveRadius * std::sin(theta);
        }
        float cosT = std::cos(theta), sinT = std::sin(theta);

        uint32_t base = static_cast<uint32_t>(out.vertices.size());
        for (const auto& v : src.vertices) {
            rs::render::MeshVertex mv = v;
            float vx = v.pos[0] * scale;
            float vy = v.pos[1] * scale;
            float vz = v.pos[2] * scale;

            float rx, ry;
            if (cantRad != 0.0f) {
                float dx = vx - pivotX, dy = vy - pivotY;
                rx = pivotX + dx * cRoll - dy * sRoll;
                ry = pivotY + dx * sRoll + dy * cRoll;
            } else {
                rx = vx; ry = vy;
            }
            float rz = vz;

            float worldX = cx + rx * cosT + rz * sinT;
            float worldY = ry;
            float worldZ = cz - rx * sinT + rz * cosT;
            mv.pos[0] = worldX; mv.pos[1] = worldY; mv.pos[2] = worldZ;

            float nx = v.normal[0], ny = v.normal[1], nz = v.normal[2];
            float nrx, nry;
            if (cantRad != 0.0f) {
                nrx = nx * cRoll - ny * sRoll;
                nry = nx * sRoll + ny * cRoll;
            } else {
                nrx = nx; nry = ny;
            }
            float nrz = nz;
            float wnx = nrx * cosT + nrz * sinT;
            float wny = nry;
            float wnz = -nrx * sinT + nrz * cosT;
            mv.normal[0] = wnx; mv.normal[1] = wny; mv.normal[2] = wnz;

            out.vertices.push_back(mv);
        }
        for (const auto& sm : src.submeshes) {
            rs::render::SubMesh osm = sm;
            osm.indexOffset = static_cast<uint32_t>(out.indices.size());
            for (uint32_t i = 0; i < sm.indexCount; ++i)
                out.indices.push_back(src.indices[sm.indexOffset + i] + base);
            out.submeshes.push_back(osm);
        }
    }
    out.ComputeAabb();
    return out;
}

// samples を弧長 s で線形補間し、位置・接線・カント角を返す(区間外は端点でクランプ)。
// samples は s 昇順(BuildRailMeshAlongSpline と同じ前提)。
static void SampleSplineAt(const std::vector<rs::world::TrackNodeSample>& samples, float s,
                           rs::core::Vec3& pos, rs::core::Vec3& fwd, float& cantRatio) {
    const rs::world::SplineBracket bracket = rs::world::LocateSplineBracketLinear(samples, s);
    const rs::world::TrackNodeSample& a = samples[bracket.lowerIndex];
    const rs::world::TrackNodeSample& b = samples[bracket.lowerIndex + 1];
    const float t = bracket.t;
    pos = a.pos + (b.pos - a.pos) * t;
    fwd = (a.tangent + (b.tangent - a.tangent) * t).Normalized();
    cantRatio = std::lerp(a.cant, b.cant, t);
}

// 1つのモデルメッシュ(src)を、任意の線形(samples)に沿って interval[m] 間隔・
// offset[m] の位相ずれで反復複製する(BuildIntervalMesh の spline 一般化、
// まくらぎのモデル表示用)。frame(right/upH)再構築とカント適用の規約は
// BuildRailMeshAlongSpline と同一(内側レール頭頂をピボットにロール)。
rs::render::Mesh BuildIntervalMeshAlongSpline(
    const rs::render::Mesh& src, float scale, float interval, float offset,
    const std::vector<rs::world::TrackNodeSample>& samples,
    float gauge, bool ignoreCant, float extraOffsetY) {
    rs::render::Mesh out;
    if (samples.size() < 2) return out;
    out.materials = src.materials;

    const rs::core::Vec3 worldUp(0, 1, 0);
    const float totalS = samples.back().s;
    const float step = (interval > rs::core::kMinRepeatIntervalM) ? interval : totalS;

    rs::core::Vec3 prevRight(1, 0, 0);
    for (float s = offset; s <= totalS + rs::core::kChainBoundaryEpsM; s += step) {
        rs::core::Vec3 pos, fwd;
        float cantRatio = 0.0f;
        SampleSplineAt(samples, std::max(s, 0.0f), pos, fwd, cantRatio);
        fwd = fwd.Normalized();

        rs::core::Vec3 right = rs::core::Vec3::Cross(worldUp, fwd).Normalized();
        if (right.Length() < rs::core::kDegenerateVectorEps) right = prevRight;
        prevRight = right;
        rs::core::Vec3 upH = rs::core::Vec3::Cross(fwd, right);

        const float cantRad = ignoreCant ? 0.0f : std::atan(cantRatio);
        const float sideSign = (cantRatio >= 0.0f) ? 1.0f : -1.0f;
        const float rollPhi = -cantRad;
        const float cRoll = std::cos(rollPhi), sRoll = std::sin(rollPhi);
        const float pivotX = sideSign * gauge * 0.5f;

        uint32_t base = static_cast<uint32_t>(out.vertices.size());
        for (const auto& v : src.vertices) {
            rs::render::MeshVertex mv = v;
            float vx = v.pos[0] * scale, vy = v.pos[1] * scale, vz = v.pos[2] * scale;
            // extraOffsetYはカント回転前に引く(BuildRailMeshAlongSplineと同じ規約)。
            float rx, ry;
            if (cantRad != 0.0f) {
                float dx = vx - pivotX, dy = vy - extraOffsetY;
                rx = pivotX + dx * cRoll - dy * sRoll;
                ry = dx * sRoll + dy * cRoll;
            } else {
                rx = vx; ry = vy - extraOffsetY;
            }
            rs::core::Vec3 world = pos + right * rx + upH * ry + fwd * vz;
            mv.pos[0] = world.x; mv.pos[1] = world.y; mv.pos[2] = world.z;

            float nx = v.normal[0], ny = v.normal[1], nz = v.normal[2];
            float nrx, nry;
            if (cantRad != 0.0f) {
                nrx = nx * cRoll - ny * sRoll;
                nry = nx * sRoll + ny * cRoll;
            } else {
                nrx = nx; nry = ny;
            }
            rs::core::Vec3 nrm = right * nrx + upH * nry + fwd * nz;
            mv.normal[0] = nrm.x; mv.normal[1] = nrm.y; mv.normal[2] = nrm.z;

            out.vertices.push_back(mv);
        }
        for (const auto& sm : src.submeshes) {
            rs::render::SubMesh osm = sm;
            osm.indexOffset = static_cast<uint32_t>(out.indices.size());
            for (uint32_t i = 0; i < sm.indexCount; ++i)
                out.indices.push_back(src.indices[sm.indexOffset + i] + base);
            out.submeshes.push_back(osm);
        }

        if (interval <= rs::core::kMinRepeatIntervalM) break; // interval無効なら1個だけ配置
    }
    out.ComputeAabb();
    return out;
}

rs::render::Mesh PlaceMeshInstance(const rs::render::Mesh& src, float scale,
                                   const rs::core::Vec3& pos, const rs::core::Vec3& fwd,
                                   const rs::core::Vec3& up) {
    rs::render::Mesh out;
    out.materials = src.materials;
    const rs::core::Vec3 fwdN = fwd.Length() > rs::core::kDegenerateVectorEps ? fwd.Normalized() : rs::core::Vec3(0, 0, 1);
    rs::core::Vec3 right = rs::core::Vec3::Cross(up, fwdN);
    right = right.Length() > rs::core::kDegenerateVectorEps ? right.Normalized() : rs::core::Vec3(1, 0, 0);
    const rs::core::Vec3 upH = rs::core::Vec3::Cross(fwdN, right);

    for (const auto& v : src.vertices) {
        rs::render::MeshVertex mv = v;
        const rs::core::Vec3 world =
            pos + right * (v.pos[0] * scale) + upH * (v.pos[1] * scale) + fwdN * (v.pos[2] * scale);
        mv.pos[0] = world.x; mv.pos[1] = world.y; mv.pos[2] = world.z;
        const rs::core::Vec3 nrm = right * v.normal[0] + upH * v.normal[1] + fwdN * v.normal[2];
        mv.normal[0] = nrm.x; mv.normal[1] = nrm.y; mv.normal[2] = nrm.z;
        out.vertices.push_back(mv);
    }
    out.indices = src.indices;
    out.submeshes = src.submeshes;
    out.ComputeAabb();
    return out;
}

} // namespace rs::legacy_plugin
