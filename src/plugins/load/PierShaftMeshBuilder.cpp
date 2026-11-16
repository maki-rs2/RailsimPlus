//
// PierShaftMeshBuilder.cpp - Pierシャフト断面(Profile)の押し出し(BuildPierShaftMesh)。
// 向き(Direction)とテーパ(TaperX/TaperY)を反映する。
//
#include "../../core/Epsilon.h"
#include "RailMeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "ProfileMaterial.h"

namespace rs::legacy_plugin {

namespace {

// テーパで断面を広げるとき、広がりの変化をこの比率ごとに区切って折れ線に近似する。
constexpr float kTaperDivRatio = 0.2f;
// 長い橋脚の分割の基準長[m]。長さ÷(この値の半分)を丸めた数以上に分ける。
constexpr float kSegmentMaxM = 100.0f;

// 橋脚の長さと広がりから、シャフトを高さ方向に何分割するか。
int ShaftDivisionCount(float length, float scaleX, float scaleY) {
    const int byTaper = 1 + static_cast<int>(std::lround((std::max(scaleX, scaleY) - 1.0f) / kTaperDivRatio));
    const int byLength = static_cast<int>(std::lround(2.0f * length / kSegmentMaxM));
    return std::max({byTaper, byLength, 1});
}

struct PierSectionScale {
    float x = 1.0f;
    float z = 1.0f;
};

PierSectionScale GetPierSectionScale(const rs::legacy_pi::PierDef& pier, float lengthM,
                                    float ratioUp) {
    ratioUp = std::clamp(ratioUp, 0.0f, 1.0f);
    return PierSectionScale{
        std::lerp(1.0f + lengthM * pier.taperX, 1.0f, ratioUp),
        std::lerp(1.0f + lengthM * pier.taperY, 1.0f, ratioUp)};
}

} // namespace

// Pier のシャフト断面(Profile)を Y 方向(高さ)へ height[m] 押し出して1メッシュ化。
// 断面は X(左右)-Z(前後) 平面(Coord の第2成分をワールドZとして扱う)。
// Rail/TieのBuildRailMeshはZ方向押し出し(断面X-Y)だったのに対し、Pierは
// 断面がX-Z(水平)でY方向(垂直)へ押し出す点が異なる。
// Direction=Up は断面のXを反転し、Vの起点を地面側にする(Downは上端が起点)。
// テーパは地面側の断面をX・Zへ(1+長さ×Taper)倍に広げ、上端を1倍にして線形につなぐ。
rs::render::Mesh BuildPierShaftMesh(const rs::legacy_pi::PluginDefinition& def,
                                    const std::wstring& baseDir, float height,
                                    std::vector<std::string>& log, int& missing) {
    rs::render::Mesh mesh;
    const rs::legacy_pi::PierDef& pier = def.pier;
    const float signX = pier.directionUp ? -1.0f : 1.0f;
    const PierSectionScale groundScale = GetPierSectionScale(pier, height, 0.0f);
    const float groundScaleX = groundScale.x;
    const float groundScaleZ = groundScale.z;
    const int divisions = ShaftDivisionCount(height, groundScaleX, groundScaleZ);

    for (const auto& prof : def.railProfiles) {
        if (prof.faces.empty()) continue;
        rs::render::Material mat;
        mat.diffuse[0] = mat.diffuse[1] = mat.diffuse[2] = 1.0f;
        mat.diffuse[3] = 1.0f;
        ApplyProfileTextures(mat, prof.texFileName, prof.useTexture, prof.materialExtra, baseDir, "pier", log, missing);
        uint32_t matIndex = static_cast<uint32_t>(mesh.materials.size());
        mesh.materials.push_back(std::move(mat));

        ProfileQuadSink quads;

        for (const auto& face : prof.faces) {
            const std::vector<rs::legacy_pi::RailProfileVertex>& vs = face.verts;
            for (size_t k = 0; k + 1 < vs.size(); ++k) {
                const rs::legacy_pi::RailProfileVertex& a = vs[k];
                const rs::legacy_pi::RailProfileVertex& b = vs[k + 1];
                // 断面辺(X,Z)と押し出し方向+Yの外積で幾何法線を求め、
                // 与えられた法線(X,Z)と逆向きなら巻き順を反転。Xの反転も反映して判定する。
                // cross((ex,0,ez),(0,1,0)) = (-ez, 0, ex)
                const float ex = signX * (b.coordX - a.coordX), ez = b.coordY - a.coordY;
                const float geomX = -ez, geomZ = ex;
                const float provX = signX * (a.normalX + b.normalX), provZ = a.normalY + b.normalY;
                const bool flip = (geomX * provX + geomZ * provZ) < 0.0f;

                // 高さ方向の分割点jの頂点を積む。jは地面側(0)から上端(divisions)へ数える。
                const auto addVert = [&](const rs::legacy_pi::RailProfileVertex& pv, int j) {
                    const float ratioUp = static_cast<float>(j) / static_cast<float>(divisions);
                    const float heightM = height * ratioUp;
                    const PierSectionScale sectionScale = GetPierSectionScale(pier, height, ratioUp);
                    // Vは始点(Down=上端、Up=地面側)から距離に比例して増える。
                    const float ratioFromBegin = pier.directionUp ? ratioUp : 1.0f - ratioUp;
                    rs::render::MeshVertex mv;
                    mv.pos[0] = signX * sectionScale.x * pv.coordX;
                    mv.pos[1] = heightM;
                    mv.pos[2] = sectionScale.z * pv.coordY;
                    float nx = signX * sectionScale.x * pv.normalX;
                    float nz = sectionScale.z * pv.normalY;
                    const float len = std::sqrt(nx * nx + nz * nz);
                    if (len > rs::core::kDegenerateVectorEps) { nx /= len; nz /= len; }
                    mv.normal[0] = nx; mv.normal[1] = 0.0f; mv.normal[2] = nz;
                    mv.uv[0] = pv.texU;
                    mv.uv[1] = prof.texVPerMeter * height * ratioFromBegin;
                    CopyProfileVertexColor(mv, pv);
                    const uint32_t idx = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(mv);
                    return idx;
                };
                for (int j = 0; j < divisions; ++j) {
                    const uint32_t a0 = addVert(a, j);
                    const uint32_t b0 = addVert(b, j);
                    const uint32_t a1 = addVert(a, j + 1);
                    const uint32_t b1 = addVert(b, j + 1);
                    if (!flip) {
                        quads.AddQuad(a, b, {a0, b0, b1, a0, b1, a1});
                    } else {
                        quads.AddQuad(a, b, {a0, b1, b0, a0, a1, b1});
                    }
                }
            }
        }
        quads.Flush(mesh, matIndex);
    }
    mesh.ComputeAabb();
    return mesh;
}

std::vector<PierIntervalPlacement> CalculatePierIntervalPlacements(
    float lengthM, float offsetM, float intervalM, float taperZ, bool directionUp) {
    std::vector<PierIntervalPlacement> placements;
    if (lengthM <= 0.0f) return placements;

    const float directedTaperZ = directionUp ? -taperZ : taperZ;
    float distanceM = offsetM;
    float scaleZ = (directedTaperZ < 0.0f) ? 1.0f - lengthM * directedTaperZ : 1.0f;
    const bool repeat = intervalM > rs::core::kMinRepeatIntervalM;

    while (distanceM < lengthM) {
        if (distanceM >= 0.0f) placements.push_back({distanceM, scaleZ});
        if (!repeat) break;
        const float stepM = intervalM * scaleZ;
        distanceM += stepM;
        scaleZ += stepM * directedTaperZ;
    }
    return placements;
}

rs::render::Mesh BuildPierIntervalMesh(const rs::render::Mesh& src, float modelScale,
                                       const rs::legacy_pi::PierDef& pier,
                                       const rs::legacy_pi::RailInterval& interval,
                                       float lengthM) {
    rs::render::Mesh mesh;
    if (lengthM <= 0.0f || src.Empty()) return mesh;

    mesh.materials = src.materials;
    const std::vector<PierIntervalPlacement> placements = CalculatePierIntervalPlacements(
        lengthM, interval.offset, interval.interval, pier.taperZ, pier.directionUp);
    for (const PierIntervalPlacement& placement : placements) {
        const float heightM = pier.directionUp ? placement.distanceM : lengthM - placement.distanceM;
        const PierSectionScale sectionScale = GetPierSectionScale(pier, lengthM, heightM / lengthM);
        const float scaleX = modelScale * sectionScale.x;
        const float scaleY = modelScale * sectionScale.z;
        const float scaleZ = modelScale * placement.scaleZ;
        const float direction = pier.directionUp ? 1.0f : -1.0f;
        const uint32_t vertexBase = static_cast<uint32_t>(mesh.vertices.size());

        for (const rs::render::MeshVertex& source : src.vertices) {
            rs::render::MeshVertex vertex = source;
            vertex.pos[0] = (pier.directionUp ? -1.0f : 1.0f) * scaleX * source.pos[0];
            vertex.pos[1] = heightM + direction * scaleZ * source.pos[2];
            vertex.pos[2] = scaleY * source.pos[1];

            float normalX = (pier.directionUp ? -1.0f : 1.0f) * source.normal[0] /
                                  ((std::abs(scaleX) > rs::core::kDegenerateVectorEps) ? scaleX : 1.0f);
            float normalY = direction * source.normal[2] /
                                  ((std::abs(scaleZ) > rs::core::kDegenerateVectorEps) ? scaleZ : 1.0f);
            float normalZ = source.normal[1] /
                                  ((std::abs(scaleY) > rs::core::kDegenerateVectorEps) ? scaleY : 1.0f);
            const float normalLength = std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
            if (normalLength > rs::core::kDegenerateVectorEps) {
                normalX /= normalLength;
                normalY /= normalLength;
                normalZ /= normalLength;
            }
            vertex.normal[0] = normalX;
            vertex.normal[1] = normalY;
            vertex.normal[2] = normalZ;
            mesh.vertices.push_back(vertex);
        }

        for (const rs::render::SubMesh& sourceSubmesh : src.submeshes) {
            rs::render::SubMesh submesh = sourceSubmesh;
            submesh.indexOffset = static_cast<uint32_t>(mesh.indices.size());
            mesh.submeshes.push_back(submesh);
            for (uint32_t index = 0; index < sourceSubmesh.indexCount; ++index) {
                mesh.indices.push_back(src.indices[sourceSubmesh.indexOffset + index] + vertexBase);
            }
        }
    }
    mesh.ComputeAabb();
    return mesh;
}

} // namespace rs::legacy_plugin
