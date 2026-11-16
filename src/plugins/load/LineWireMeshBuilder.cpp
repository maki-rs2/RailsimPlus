//
// LineWireMeshBuilder.cpp - Line(架線)の径間ワイヤーメッシュ生成(BuildLineWireMesh)。
// 描画元はWiredmesh(RailSim II -k-build、実架線本体)
// 優先、無ければWireframe(旧形式)。WiredmeshにFace断面(RailProfileFaceと同一スキーマ)が
// あればBuildRailMeshAlongSplineと同じ要領でLine{}ポリライン沿いにスイープしテクスチャ付き
// メッシュにする。Faceが無いプロファイル(旧形式Wireframe等)は RailSim II の線プリミティブを
// 三角形限定の描画パイプラインへ落とすため、薄いリボン(帯)で近似する。
//
#include "RailMeshBuilder.h"

#include "../../core/Epsilon.h"
#include "ProfileMaterial.h"

#include <cmath>

namespace rs::legacy_plugin {

namespace {

using rs::core::Vec3;
using rs::render::Mesh;
using rs::render::MeshVertex;

void PushVertex(Mesh& mesh, const Vec3& p, const Vec3& n, float u = 0.0f, float v = 0.0f) {
    MeshVertex mv;
    mv.pos[0] = p.x; mv.pos[1] = p.y; mv.pos[2] = p.z;
    mv.normal[0] = n.x; mv.normal[1] = n.y; mv.normal[2] = n.z;
    mv.uv[0] = u; mv.uv[1] = v;
    mesh.vertices.push_back(mv);
}

// axisへhalfWidthだけ+/-に押し出した1区間ぶんの帯を積む。CullMode=BACKの
// 通常パイプラインでは片面のみだと視点によって消えるため、表裏両面(winding
// を反転した2組の三角形)を積む。Face断面を持たないプロファイル向けのフォールバック。
void AppendRibbonSegment(Mesh& mesh, const Vec3& p0, const Vec3& p1, const Vec3& axis,
                         float halfWidth) {
    const Vec3 a0 = p0 - axis * halfWidth;
    const Vec3 a1 = p1 - axis * halfWidth;
    const Vec3 b0 = p0 + axis * halfWidth;
    const Vec3 b1 = p1 + axis * halfWidth;
    const Vec3 normal = Vec3::Cross(a1 - a0, b0 - a0).Normalized();
    const Vec3 backNormal = normal * -1.0f;

    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    PushVertex(mesh, a0, normal);
    PushVertex(mesh, b0, normal);
    PushVertex(mesh, b1, normal);
    PushVertex(mesh, a1, normal);
    PushVertex(mesh, a0, backNormal);
    PushVertex(mesh, b0, backNormal);
    PushVertex(mesh, b1, backNormal);
    PushVertex(mesh, a1, backNormal);

    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
    mesh.indices.push_back(base + 4);
    mesh.indices.push_back(base + 6);
    mesh.indices.push_back(base + 5);
    mesh.indices.push_back(base + 4);
    mesh.indices.push_back(base + 7);
    mesh.indices.push_back(base + 6);
}

void AppendRibbon(Mesh& mesh, const std::vector<Vec3>& pts, const Vec3& axis, float halfWidth) {
    for (size_t i = 0; i + 1 < pts.size(); ++i)
        AppendRibbonSegment(mesh, pts[i], pts[i + 1], axis, halfWidth);
}

// 径間の向き(dirSpan)・右(right)・上(up)を基準に、頂点ごとの断面フレームの右と上を求める。
// 頂点のDir/Upを径間の座標系へ移し、right=cross(Up, Dir)・up=cross(Dir, right)で直交させる。
// DirとUpがほぼ平行で右が決まらない頂点は、ポリラインの接線から組み直す。
void BuildSectionFrames(const std::vector<Vec3>& pts,
                        const std::vector<rs::legacy_pi::LineWireVertex>& verts,
                        const Vec3& right, const Vec3& up, const Vec3& dirSpan,
                        std::vector<Vec3>& rightVec, std::vector<Vec3>& upVec) {
    rightVec.assign(pts.size(), Vec3(1, 0, 0));
    upVec.assign(pts.size(), Vec3(0, 1, 0));
    for (size_t i = 0; i < pts.size(); ++i) {
        const rs::legacy_pi::LineWireVertex& v = verts[i];
        const Vec3 dirW = right * v.dir.x + up * v.dir.y + dirSpan * v.dir.z;
        const Vec3 upW = right * v.up.x + up * v.up.y + dirSpan * v.up.z;
        Vec3 r = Vec3::Cross(upW, dirW);
        if (r.Length() >= static_cast<float>(rs::core::kDegenerateVectorEps)) {
            rightVec[i] = r.Normalized();
            upVec[i] = Vec3::Cross(dirW.Normalized(), rightVec[i]).Normalized();
            continue;
        }
        Vec3 d;
        if (i == 0) d = pts[1] - pts[0];
        else if (i + 1 == pts.size()) d = pts[i] - pts[i - 1];
        else d = pts[i + 1] - pts[i - 1];
        const Vec3 tangent = d.Length() > static_cast<float>(rs::core::kDegenerateVectorEps) ? d.Normalized() : dirSpan;
        r = Vec3::Cross(up, tangent);
        rightVec[i] = r.Length() < static_cast<float>(rs::core::kDegenerateVectorEps) ? (i > 0 ? rightVec[i - 1] : right) : r.Normalized();
        upVec[i] = Vec3::Cross(tangent, rightVec[i]).Normalized();
    }
}

// 頂点ごとのV座標。先頭はTexV指定があればその値(無ければ0)、以降は前の頂点のVに
// 区間の長さ×texVPerMeterを足す。TexVを持つ頂点は、足した値を0から遠い方へ丸めてTexVを足す。
std::vector<float> BuildSweepV(const std::vector<Vec3>& pts,
                               const std::vector<rs::legacy_pi::LineWireVertex>& verts,
                               float texVPerMeter) {
    std::vector<float> vCoord(pts.size(), 0.0f);
    if (verts[0].texV >= 0.0f) vCoord[0] = verts[0].texV;
    for (size_t i = 1; i < pts.size(); ++i) {
        vCoord[i] = vCoord[i - 1] + (pts[i] - pts[i - 1]).Length() * texVPerMeter;
        if (verts[i].texV >= 0.0f) vCoord[i] = std::round(vCoord[i]) + verts[i].texV;
    }
    return vCoord;
}

// worldPts(架線ポリラインのワールド座標列)沿いにfacesの2D断面をスイープする
// (BuildRailMeshAlongSpline: RailMeshBuilder.cpp の押し出しロジックの独立実装。
// カントは架線に存在しないため常に0、gauge/pivotも不要)。各点の断面フレームは
// 頂点のDir/Upから組む(BuildSectionFrames)。V座標はBuildSweepV。
void SweepFacesAlongPoints(Mesh& mesh, ProfileQuadSink& quads,
                           const std::vector<rs::legacy_pi::RailProfileFace>& faces,
                           float texVPerMeter, const std::vector<Vec3>& pts,
                           const std::vector<rs::legacy_pi::LineWireVertex>& verts,
                           const Vec3& right, const Vec3& up, const Vec3& dirSpan) {
    if (pts.size() < 2 || verts.size() != pts.size()) return;

    std::vector<Vec3> rightVec, upVec;
    BuildSectionFrames(pts, verts, right, up, dirSpan, rightVec, upVec);
    const std::vector<float> vCoord = BuildSweepV(pts, verts, texVPerMeter);

    for (const rs::legacy_pi::RailProfileFace& face : faces) {
        const std::vector<rs::legacy_pi::RailProfileVertex>& vs = face.verts;
        for (size_t k = 0; k + 1 < vs.size(); ++k) {
            const rs::legacy_pi::RailProfileVertex& a = vs[k];
            const rs::legacy_pi::RailProfileVertex& b = vs[k + 1];
            // 断面辺a->bと押し出し方向の外積(2D相当)で幾何法線を求め、与えられた
            // 法線と逆向きなら巻き順を反転する(RailMeshBuilder.cppと同じ判定)。
            const float ex = b.coordX - a.coordX, ey = b.coordY - a.coordY;
            const float geomX = ey, geomY = -ex;
            const float provX = a.normalX + b.normalX, provY = a.normalY + b.normalY;
            const bool flip = (geomX * provX + geomY * provY) < 0.0f;

            auto addVert = [&](const rs::legacy_pi::RailProfileVertex& pv, size_t i) -> uint32_t {
                const Vec3& r = rightVec[i];
                const Vec3& u = upVec[i];
                const Vec3 world = pts[i] + r * pv.coordX + u * pv.coordY;
                float nx = pv.normalX, ny = pv.normalY;
                const float len = std::sqrt(nx * nx + ny * ny);
                if (len > static_cast<float>(rs::core::kDegenerateVectorEps)) { nx /= len; ny /= len; }
                const Vec3 nrm = r * nx + u * ny;
                PushVertex(mesh, world, nrm, pv.texU, vCoord[i]);
                CopyProfileVertexColor(mesh.vertices.back(), pv);
                return static_cast<uint32_t>(mesh.vertices.size() - 1);
            };

            uint32_t prevA = 0, prevB = 0;
            for (size_t i = 0; i < pts.size(); ++i) {
                const uint32_t curA = addVert(a, i);
                const uint32_t curB = addVert(b, i);
                if (i > 0) {
                    if (!flip)
                        quads.AddQuad(a, b, {prevA, prevB, curB, prevA, curB, curA});
                    else
                        quads.AddQuad(a, b, {prevA, curB, prevB, prevA, curA, curB});
                }
                prevA = curA;
                prevB = curB;
            }
        }
    }
}

// 描画に使う1本の架線プロファイルへの参照。Wiredmesh/Wireframeのどちらから来たかを
// ここで吸収し、以降の描画コードを共通化する。facesが非空ならFace断面スイープ、
// 空ならDiffuseリボンへフォールバックする。
struct WireProfileRef {
    const std::vector<rs::legacy_pi::LineWirePolyline>* lines;
    float r, g, b, a;
    const std::vector<rs::legacy_pi::RailProfileFace>* faces = nullptr;
    std::string texFileName;
    float texVPerMeter = 1.0f;
    bool useTexture = false;
    rs::legacy_pi::ProfileMaterialExtra materialExtra; // Wireframe(旧形式)は既定のまま
};

} // namespace

rs::render::Mesh BuildLineWireMesh(const rs::legacy_pi::LineDef& lineDef,
                                   const std::wstring& baseDir, const Vec3& baseA,
                                   const Vec3& upA, const Vec3& baseB, const Vec3& upB,
                                   std::vector<std::string>& log, int& missingTex) {
    rs::render::Mesh mesh;
    if (lineDef.wiredmeshes.empty() && lineDef.wireframes.empty()) return mesh;

    const Vec3 chord = baseB - baseA;
    const float spanLength = chord.Length();
    if (spanLength < rs::core::kWireSpanLengthEpsM) return mesh;

    // 描画元の選定。
    // RailSim II -k-build(*2k.txt)にはWiredmesh(実架線本体)とWireframe(ハンガー取付点
    // トラッキング、Diffuseなし)が混ざっているため、Wiredmeshがあれば必ずそちらを
    // 使う(取り違えるとハンガートラッキングの平坦な線を架線として描いてしまう)。
    // Wiredmeshは区間が重複しうる(トロリ線=区間指定なし=常時、messenger=径間長
    // 帯別)ため単一選択ではなく[minInterval, maxInterval)を満たす全プロファイルを
    // 重ねて描画する。Wireframeのみの旧形式ファイルでは単一プロファイル選択
    // (一致が無ければmaxIntervalが最大のブロックへフォールバック)を維持する。
    std::vector<WireProfileRef> selected;
    if (!lineDef.wiredmeshes.empty()) {
        auto toRef = [](const rs::legacy_pi::LineWiredmesh& w) {
            WireProfileRef ref;
            ref.lines = &w.lines;
            ref.r = w.diffuseR; ref.g = w.diffuseG; ref.b = w.diffuseB; ref.a = w.diffuseA;
            if (!w.faces.empty()) ref.faces = &w.faces;
            ref.texFileName = w.texFileName;
            ref.texVPerMeter = w.texVPerMeter;
            ref.useTexture = w.useTexture;
            ref.materialExtra = w.materialExtra;
            return ref;
        };
        for (const auto& w : lineDef.wiredmeshes)
            if (spanLength >= w.minInterval && spanLength < w.maxInterval)
                selected.push_back(toRef(w));
        if (selected.empty()) {
            const rs::legacy_pi::LineWiredmesh* best = nullptr;
            for (const auto& w : lineDef.wiredmeshes)
                if (!best || w.maxInterval > best->maxInterval) best = &w;
            if (best) selected.push_back(toRef(*best));
        }
    } else {
        const rs::legacy_pi::LineWireframe* wf = nullptr;
        for (const auto& w : lineDef.wireframes) {
            if (spanLength >= w.minInterval && spanLength < w.maxInterval) {
                wf = &w;
                break;
            }
        }
        if (!wf) {
            for (const auto& w : lineDef.wireframes)
                if (!wf || w.maxInterval > wf->maxInterval) wf = &w;
        }
        if (wf) selected.push_back({&wf->lines, wf->diffuseR, wf->diffuseG, wf->diffuseB, wf->diffuseA});
    }
    if (selected.empty()) return mesh;

    const Vec3 dirSpan = chord * (1.0f / spanLength);
    Vec3 up = upA + upB;
    up = up.Length() > static_cast<float>(rs::core::kDegenerateVectorEps) ? up.Normalized() : Vec3(0, 1, 0);
    // dirSpanとupがほぼ平行(急勾配edge等)だと外積が不定になるため、その場合は
    // ワールドUPへフォールバックする。
    if (std::abs(Vec3::Dot(up, dirSpan)) > 0.999f) up = Vec3(0, 1, 0);
    Vec3 right = Vec3::Cross(up, dirSpan).Normalized();
    up = Vec3::Cross(dirSpan, right).Normalized(); // 直交化

    // 架線の見かけ太さ(半幅)[m]。Face断面を持たないプロファイル(旧形式Wireframe等)の
    // リボン近似用。RailSim IIのデータはx成分を持たない(=太さ0の線プリミティブ)ため実寸の
    // 出典が無く、視認できる程度の装飾的な既定値とする。
    constexpr float kHalfWidth = 0.015f;

    // selected各プロファイルごとに専用マテリアル+サブメッシュを積む(トロリ線と
    // messengerで断面/色/テクスチャが異なりうるため単一マテリアルに統合しない)。
    for (const WireProfileRef& profile : selected) {
        rs::render::Material mat;
        mat.diffuse[0] = profile.r;
        mat.diffuse[1] = profile.g;
        mat.diffuse[2] = profile.b;
        mat.diffuse[3] = profile.a;
        if (profile.faces) {
            ApplyProfileTextures(mat, profile.texFileName, profile.useTexture, profile.materialExtra, baseDir,
                                 "line wire", log, missingTex);
        }
        const uint32_t matIndex = static_cast<uint32_t>(mesh.materials.size());
        mesh.materials.push_back(mat);

        // Face断面のスイープは四角形を quads に集め、最後に submesh として出す。リボンは index を直接積む。
        ProfileQuadSink quads;
        const uint32_t indexStart = static_cast<uint32_t>(mesh.indices.size());
        for (const rs::legacy_pi::LineWirePolyline& poly : *profile.lines) {
            if (poly.verts.size() < 2) continue;
            std::vector<Vec3> worldPts;
            worldPts.reserve(poly.verts.size());
            for (const rs::legacy_pi::LineWireVertex& v : poly.verts)
                worldPts.push_back(baseA + dirSpan * (v.z * spanLength) + up * v.y);
            if (profile.faces) {
                SweepFacesAlongPoints(mesh, quads, *profile.faces, profile.texVPerMeter, worldPts, poly.verts,
                                      right, up, dirSpan);
            } else {
                // "+"断面(right方向・up方向にそれぞれ薄い帯)にし、どの角度から見ても
                // 消えないようにする(単一平面のリボンだと視線が面と平行なとき消える)。
                AppendRibbon(mesh, worldPts, right, kHalfWidth);
                AppendRibbon(mesh, worldPts, up, kHalfWidth);
            }
        }
        if (profile.faces) {
            quads.Flush(mesh, matIndex);
            continue;
        }
        const uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size()) - indexStart;
        if (indexCount > 0) {
            rs::render::SubMesh sm;
            sm.materialIndex = matIndex;
            sm.indexOffset = indexStart;
            sm.indexCount = indexCount;
            mesh.submeshes.push_back(sm);
        }
    }
    if (mesh.indices.empty()) return rs::render::Mesh{};

    mesh.ComputeAabb();
    return mesh;
}

} // namespace rs::legacy_plugin
