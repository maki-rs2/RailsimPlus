//
// RailMeshBuilder.cpp - Rail断面プロファイルの押し出しメッシュ生成
// (直線/円弧の単発押し出しBuildRailMesh、任意線形に沿った押し出し
// BuildRailMeshAlongSpline)。Pier/反復複製メッシュはPierMeshBuilder.cppへ分離。
//
#include "RailMeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "../../core/Epsilon.h"
#include "ProfileMaterial.h"
#include "../../core/Math.h"

namespace rs::legacy_plugin {

float ComputeCantDegrees(const rs::legacy_pi::PluginDefinition& def, float curveRadius) {
    const float absRadius = std::fabs(curveRadius);
    if (absRadius < rs::core::kStraightRadiusEpsM) return 0.0f;                 // 直線はカント無し
    if (def.maxCant <= rs::core::kCantRatioEps || def.cantRatio <= rs::core::kCantRatioEps) return 0.0f;
    return def.maxCant * (1.0f - std::exp(-def.cantRatio / (def.maxCant * absRadius)));
}

// Rail 断面プロファイルを Z 方向へ length[m] 押し出して1メッシュ化する。
// 断面は X(左右)-Y(上下) 平面。各 Profile が1サブメッシュ（1マテリアル）。
// 各 Face のポリライン隣接頂点間を四角形として張る。
// curveRadius が 0 に近ければ直線、それ以外なら半径 curveRadius[m] の円弧に沿って押し出す
// (curveRadius > 0 は +X 方向へ曲がる円弧)。曲線では RailInfo のカントを断面へ適用する。
rs::render::Mesh BuildRailMesh(const rs::legacy_pi::PluginDefinition& def,
                               const std::wstring& baseDir, float length,
                               float curveRadius,
                               std::vector<std::string>& log, int& missing,
                               float cantDegOverride) {
    rs::render::Mesh mesh;
    const bool straight = std::fabs(curveRadius) < rs::core::kStraightRadiusEpsM;

    // --- カント(横断傾斜)のセットアップ -------------------------------------
    // 断面ローカル (coordX,coordY) 平面内での剛体回転で表現する。回転は接線(Z)軸
    // まわりのロールに相当し、円弧配置(Y軸ヘディング回転)の前段で適用する。
    // 内側レール(=カーブ内側)を軸に、外側レールが上がる向きに傾ける。
    // curveRadius>0 は +X 側へ曲がる → 内側=+X 側。内側レール頭頂を軸に固定。
    const float cantDeg = (cantDegOverride >= 0.0f) ? cantDegOverride
                                                     : ComputeCantDegrees(def, curveRadius);
    const float cantRad = cantDeg * std::numbers::pi_v<float> / 180.0f;
    const float sideSign = (curveRadius >= 0.0f) ? 1.0f : -1.0f; // 内側の符号
    const float rollPhi = -sideSign * cantRad;   // ローカル(x,y)へ適用するロール角
    const float cphi = std::cos(rollPhi), sphi = std::sin(rollPhi);
    float railTopY = 0.0f;                        // レール頭頂(=回転軸のY)
    for (const auto& prof : def.railProfiles)
        for (const auto& face : prof.faces)
            for (const auto& v : face.verts)
                railTopY = std::max(railTopY, v.coordY);
    const float pivotX = sideSign * def.gauge * 0.5f; // 内側レール位置
    const float pivotY = railTopY;
    // 頂点座標にカントを適用（ignoreCant または カント0 はそのまま通す）。
    auto cantPos = [&](float x, float y, bool ignore, float& ox, float& oy) {
        if (ignore || cantRad == 0.0f) { ox = x; oy = y; return; }
        float dx = x - pivotX, dy = y - pivotY;
        ox = pivotX + dx * cphi - dy * sphi;
        oy = pivotY + dx * sphi + dy * cphi;
    };
    // 法線にカントを適用（平行移動成分なしの回転のみ）。
    auto cantNrm = [&](float nx, float ny, bool ignore, float& onx, float& ony) {
        if (ignore || cantRad == 0.0f) { onx = nx; ony = ny; return; }
        onx = nx * cphi - ny * sphi;
        ony = nx * sphi + ny * cphi;
    };
    for (const auto& prof : def.railProfiles) {
        if (prof.faces.empty()) continue;
        rs::render::Material mat;
        mat.diffuse[0] = mat.diffuse[1] = mat.diffuse[2] = 1.0f;
        mat.diffuse[3] = 1.0f;
        ApplyProfileTextures(mat, prof.texFileName, prof.useTexture, prof.materialExtra, baseDir, "rail", log, missing);
        uint32_t matIndex = static_cast<uint32_t>(mesh.materials.size());
        mesh.materials.push_back(std::move(mat));

        ProfileQuadSink quads;

        const float vAtEnd = prof.texVPerMeter * length;

        // 円弧押し出し用: リング分割数を1リング概ね3度以下になるように決める。
        int segCount = 1;
        if (!straight) {
            const float angleStepRad = 3.0f * std::numbers::pi_v<float> / 180.0f;
            float thetaTotal = length / curveRadius;
            segCount = static_cast<int>(std::ceil(std::fabs(thetaTotal) / angleStepRad));
            segCount = std::clamp(segCount, 2, 256);
        }

        for (const auto& face : prof.faces) {
            const std::vector<rs::legacy_pi::RailProfileVertex>& vs = face.verts;
            for (size_t k = 0; k + 1 < vs.size(); ++k) {
                const rs::legacy_pi::RailProfileVertex& a = vs[k];
                const rs::legacy_pi::RailProfileVertex& b = vs[k + 1];

                if (straight) {
                    auto addVert = [&](const rs::legacy_pi::RailProfileVertex& pv, float z,
                                       float v) {
                        rs::render::MeshVertex mv;
                        mv.pos[0] = pv.coordX; mv.pos[1] = pv.coordY; mv.pos[2] = z;
                        float nx = pv.normalX, ny = pv.normalY, nz = 0.0f;
                        float len = std::sqrt(nx * nx + ny * ny);
                        if (len > rs::core::kDegenerateVectorEps) { nx /= len; ny /= len; }
                        mv.normal[0] = nx; mv.normal[1] = ny; mv.normal[2] = nz;
                        mv.uv[0] = pv.texU; mv.uv[1] = v;
                        CopyProfileVertexColor(mv, pv);
                        uint32_t idx = static_cast<uint32_t>(mesh.vertices.size());
                        mesh.vertices.push_back(mv);
                        return idx;
                    };
                    uint32_t a0 = addVert(a, 0.0f, 0.0f);
                    uint32_t b0 = addVert(b, 0.0f, 0.0f);
                    uint32_t a1 = addVert(a, length, vAtEnd);
                    uint32_t b1 = addVert(b, length, vAtEnd);
                    // 四角形 (a0,a1,b1,b0) を2三角形に(RailSim IIのCProfilePlugin::Dumpと
                    // 同じ頂点順序、railsim2_ref/CProfilePlugin.cpp参照。巻き順の反転
                    // 判定は行わずFace記載順をそのまま使う)。
                    quads.AddQuad(a, b, {a0, b1, b0, a0, a1, b1});
                } else {
                    // 円弧押し出し: theta_i = s_i / curveRadius のリングを
                    // segCount 回繰り返し、隣接リング間を四角形として張る。
                    auto addVert = [&](const rs::legacy_pi::RailProfileVertex& pv,
                                       float cosT, float sinT, float posX,
                                       float posZ, float v) {
                        rs::render::MeshVertex mv;
                        // カント適用後のローカル断面座標・法線
                        float lx, ly; cantPos(pv.coordX, pv.coordY, pv.ignoreCant, lx, ly);
                        float nx, ny; cantNrm(pv.normalX, pv.normalY, pv.ignoreCant, nx, ny);
                        float worldX = posX + lx * cosT;
                        float worldY = ly;
                        float worldZ = posZ - lx * sinT;
                        mv.pos[0] = worldX; mv.pos[1] = worldY; mv.pos[2] = worldZ;
                        float len = std::sqrt(nx * nx + ny * ny);
                        if (len > rs::core::kDegenerateVectorEps) { nx /= len; ny /= len; }
                        mv.normal[0] = nx * cosT; mv.normal[1] = ny;
                        mv.normal[2] = -nx * sinT;
                        mv.uv[0] = pv.texU; mv.uv[1] = v;
                        CopyProfileVertexColor(mv, pv);
                        uint32_t idx = static_cast<uint32_t>(mesh.vertices.size());
                        mesh.vertices.push_back(mv);
                        return idx;
                    };

                    uint32_t prevA = 0, prevB = 0;
                    for (int i = 0; i <= segCount; ++i) {
                        float sI = length * static_cast<float>(i) / static_cast<float>(segCount);
                        float thetaI = sI / curveRadius;
                        float cosT = std::cos(thetaI);
                        float sinT = std::sin(thetaI);
                        float posX = curveRadius * (1.0f - cosT);
                        float posZ = curveRadius * sinT;
                        float v = prof.texVPerMeter * sI;

                        uint32_t curA = addVert(a, cosT, sinT, posX, posZ, v);
                        uint32_t curB = addVert(b, cosT, sinT, posX, posZ, v);

                        if (i > 0) {
                            uint32_t a0 = prevA, b0 = prevB, a1 = curA, b1 = curB;
                            // 直線ブランチと同じ固定順序(RailSim IIと同じ、巻き順反転判定は行わない)。
                            quads.AddQuad(a, b, {a0, b1, b0, a0, a1, b1});
                        }
                        prevA = curA;
                        prevB = curB;
                    }
                }
            }
        }
        quads.Flush(mesh, matIndex);
    }
    mesh.ComputeAabb();
    return mesh;
}

// Rail 断面プロファイルを、任意の線形（TrackNodeSample 列）に沿って押し出して
// 1メッシュ化する（BuildRailMesh の一般化）。既存 BuildRailMesh は温存し、この
// 関数は独立実装とする（回帰リスク回避）。カント規約は BuildRailMesh 円弧
// ブランチと同一（内側レール頭頂をピボットにロール）。sample.up は使わず、
// 各サンプルで right/upH を fwd から再構築する。
rs::render::Mesh BuildRailMeshAlongSpline(
    const rs::legacy_pi::PluginDefinition& def,
    const std::wstring& baseDir,
    const std::vector<rs::world::TrackNodeSample>& samples,
    std::vector<std::string>& log, int& missing,
    float extraOffsetY) {
    rs::render::Mesh mesh;
    if (samples.size() < 2) return mesh;

    const rs::core::Vec3 worldUp(0, 1, 0);

    // レール頭頂Y（回転軸のローカルY）は全断面共通としてBuildRailMeshと同様に求める。
    float railTopY = 0.0f;
    for (const auto& prof : def.railProfiles)
        for (const auto& face : prof.faces)
            for (const auto& v : face.verts)
                railTopY = std::max(railTopY, v.coordY);

    // サンプルごとのカントロール角[rad]を事前計算。samples[i].cantは RailSim II の`Cant`と
    // 同じtan比(横断傾斜のtanθ、度ではない)で格納されて
    // いるため、ロール角はatan(cant)で求める(cant[deg]として*pi/180すると、実際の傾斜より
    // レールが著しく小さくしか傾かない)。
    // sideSign = (cant>=0)?+1:-1、rollPhi = -sideSign*|cantRad| = -cantRad。
    // pivotX = sideSign * gauge/2（内側レール位置）。
    std::vector<float> rollPhi(samples.size());
    std::vector<float> pivotX(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        const float cantRad = std::atan(samples[i].cant);
        const float sideSign = (samples[i].cant >= 0.0f) ? 1.0f : -1.0f;
        rollPhi[i] = -cantRad;
        pivotX[i] = sideSign * def.gauge * 0.5f;
    }

    // 頂点座標へカントを適用（ignoreCant または cant==0 はそのまま通す）。
    auto cantPos = [&](size_t i, float x, float y, bool ignore, float& ox, float& oy) {
        const float phi = rollPhi[i];
        if (ignore || phi == 0.0f) { ox = x; oy = y; return; }
        const float cphi = std::cos(phi), sphi = std::sin(phi);
        const float dx = x - pivotX[i], dy = y - railTopY;
        ox = pivotX[i] + dx * cphi - dy * sphi;
        oy = railTopY + dx * sphi + dy * cphi;
    };
    // 法線へカントを適用（回転のみ）。
    auto cantNrm = [&](size_t i, float nx, float ny, bool ignore, float& onx, float& ony) {
        const float phi = rollPhi[i];
        if (ignore || phi == 0.0f) { onx = nx; ony = ny; return; }
        const float cphi = std::cos(phi), sphi = std::sin(phi);
        onx = nx * cphi - ny * sphi;
        ony = nx * sphi + ny * cphi;
    };

    // 各サンプルの断面フレーム（right/upH）を事前計算。
    std::vector<rs::core::Vec3> rightVec(samples.size()), upVec(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        rs::core::Vec3 fwd = samples[i].tangent.Normalized();
        rs::core::Vec3 right = rs::core::Vec3::Cross(worldUp, fwd).Normalized();
        if (right.Length() < rs::core::kDegenerateVectorEps) {
            // fwd がworldUpとほぼ平行（急勾配や真上向き）な退化ケース: 前サンプルの
            // right を流用するか、無ければ+X既定にフォールバック。
            right = (i > 0) ? rightVec[i - 1] : rs::core::Vec3(1, 0, 0);
        }
        rs::core::Vec3 upH = rs::core::Vec3::Cross(fwd, right);
        rightVec[i] = right;
        upVec[i] = upH;
    }

    for (const auto& prof : def.railProfiles) {
        if (prof.faces.empty()) continue;
        rs::render::Material mat;
        mat.diffuse[0] = mat.diffuse[1] = mat.diffuse[2] = 1.0f;
        mat.diffuse[3] = 1.0f;
        ApplyProfileTextures(mat, prof.texFileName, prof.useTexture, prof.materialExtra, baseDir, "rail", log, missing);
        uint32_t matIndex = static_cast<uint32_t>(mesh.materials.size());
        mesh.materials.push_back(std::move(mat));

        ProfileQuadSink quads;

        for (const auto& face : prof.faces) {
            const std::vector<rs::legacy_pi::RailProfileVertex>& vs = face.verts;
            for (size_t k = 0; k + 1 < vs.size(); ++k) {
                const rs::legacy_pi::RailProfileVertex& a = vs[k];
                const rs::legacy_pi::RailProfileVertex& b = vs[k + 1];

                auto addVert = [&](const rs::legacy_pi::RailProfileVertex& pv, size_t i,
                                   float v) {
                    rs::render::MeshVertex mv;
                    // extraOffsetYはカント回転前に引く(RailSim II のAfterDump同様、下位レイヤの
                    // 沈め量がカントロールと一緒に回転するようにする)。
                    float lx, ly; cantPos(i, pv.coordX, pv.coordY - extraOffsetY,
                                          pv.ignoreCant, lx, ly);
                    float nx, ny; cantNrm(i, pv.normalX, pv.normalY, pv.ignoreCant, nx, ny);
                    const rs::core::Vec3& right = rightVec[i];
                    const rs::core::Vec3& upH = upVec[i];
                    rs::core::Vec3 world = samples[i].pos + right * lx + upH * ly;
                    mv.pos[0] = world.x; mv.pos[1] = world.y; mv.pos[2] = world.z;
                    float len = std::sqrt(nx * nx + ny * ny);
                    if (len > rs::core::kDegenerateVectorEps) { nx /= len; ny /= len; }
                    rs::core::Vec3 nrm = right * nx + upH * ny;
                    mv.normal[0] = nrm.x; mv.normal[1] = nrm.y; mv.normal[2] = nrm.z;
                    mv.uv[0] = pv.texU; mv.uv[1] = v;
                    CopyProfileVertexColor(mv, pv);
                    uint32_t idx = static_cast<uint32_t>(mesh.vertices.size());
                    mesh.vertices.push_back(mv);
                    return idx;
                };

                uint32_t prevA = 0, prevB = 0;
                for (size_t i = 0; i < samples.size(); ++i) {
                    float v = prof.texVPerMeter * samples[i].s;
                    uint32_t curA = addVert(a, i, v);
                    uint32_t curB = addVert(b, i, v);

                    if (i > 0) {
                        uint32_t a0 = prevA, b0 = prevB, a1 = curA, b1 = curB;
                        // BuildRailMeshと同じ固定順序(RailSim IIと同じ、巻き順反転判定は行わない)。
                        quads.AddQuad(a, b, {a0, b1, b0, a0, a1, b1});
                    }
                    prevA = curA;
                    prevB = curB;
                }
            }
        }
        quads.Flush(mesh, matIndex);
    }
    mesh.ComputeAabb();
    return mesh;
}

} // namespace rs::legacy_plugin
