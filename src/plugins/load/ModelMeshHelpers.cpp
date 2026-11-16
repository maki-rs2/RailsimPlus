//
// ModelMeshHelpers.cpp - ModelMeshHelpers の実装(PluginLoader.cppから分離)
//
#include "ModelMeshHelpers.h"

#include <cstring>

#include "PluginPaths.h"
#include "../../platform/text/Encoding.h"
#include "../../render/resource/TextureLoader.h"

namespace rs::legacy_plugin {

namespace {

// ChangeTexture 1件のパスを解決し、差し替え先が抜き穴を持つかを判定する。
// useEnvMap / useBumpMap は対象材質が EnvMap / BumpMap で指定されうるか(発動条件は見ず、
// パーツの envMaps / bumpMaps に1件でも載っていれば真)。ここで組み立てた法線/ORMを実際に
// 使うかは、条件ごとに決まる実行時のフラグ(MaterialApplyResult::AllowsNormal/AllowsOrm)で決める。
ResolvedPartChangeTexture ResolveChangeTexture(const rs::legacy_pi::ChangeTextureRef& ct,
                                               bool useEnvMap, bool useBumpMap,
                                               const std::wstring& baseDir,
                                               std::vector<std::string>& log, int& missing) {
    ResolvedPartChangeTexture out;
    auto resolveInto = [&](const std::string& rel, std::string& dst, const char* what) {
        if (rel.empty()) return;
        std::wstring wp = ResolvePath(baseDir, rel);
        if (FileExists(wp)) {
            dst = rs::platform_text::WideToUtf8(wp);
        } else {
            log.push_back(std::string("[plugin][warn] missing ") + what + " tex: " + rel);
            ++missing;
        }
    };
    // ベース画像の隣の「<画像名><suffix>.<拡張子>」があれば dst に入れる(無ければ何もしない)。
    auto pairBesideBase = [&](const wchar_t* suffix, std::string& dst) {
        if (ct.base.empty()) return;
        const std::string paired = FindPairedTexture(ResolvePath(baseDir, ct.base), suffix);
        if (!paired.empty()) dst = paired;
    };
    resolveInto(ct.base, out.paths.base, "base");
    // RailSim II -k-build: 法線は EnvMap か BumpMap の指定がある材質にだけ反映され、ORM は EnvMap 指定の材質にだけ反映される
    // (指定が無い材質ではテクスチャを指定しても読み込まない)。
    if (useEnvMap || useBumpMap) {
        resolveInto(ct.normal, out.paths.normal, "normal");
        // BumpMap 指定で法線パスが無ければ、ベース画像の隣の「<画像名>_n.png」を法線マップに使う
        // (BumpMap だけを書く実データの約束事。無ければ法線なしのまま)。
        if (useBumpMap && ct.normal.empty()) pairBesideBase(L"_n", out.paths.normal);
    }
    if (useEnvMap) {
        resolveInto(ct.pbr, out.paths.pbr, "orm");
        // EnvMap 指定で ORM パスが無ければ、ベース画像の隣の「<画像名>_orm.png」を使う(RailSim II -k-build の exe に接尾辞の文字列がある)。
        if (ct.pbr.empty()) pairBesideBase(L"_orm", out.paths.pbr);
    }
    // 実行時に「抜き穴を持つテクスチャへ」差し替わりうる印。Case/If 条件付き
    // 候補も含めて見る（条件次第で non2.png 等の透明テクスチャへ切り替わりうる
    // ため）が、候補画像に alpha==0 の texel が1つも無ければ立てない。
    // この印は後段で cutout(alpha test)へ昇格させるために使うので、「差し替え
    // 宣言があるか」ではなく「差し替え先が clip が要るか」で決める必要がある。
    // 宣言の有無で決めると、同じモデル・同じ材質でも宣言のあるプラグインだけが
    // cutout へ回り、描画経路の違いがそのまま見た目の差になる
    // 宣言はあるが解決に失敗した候補(missing)は中身を確かめられないため
    // 印を立てる側に倒す。
    const bool unresolvedCandidate = !ct.base.empty() && out.paths.base.empty();
    out.swappable = unresolvedCandidate || rs::render::ImageHasZeroAlphaTexel(out.paths.base);
    return out;
}

} // namespace

int ApplyPbr(rs::render::Mesh& mesh, const rs::legacy_pi::ModelPart& part,
             const std::wstring& baseDir, std::vector<std::string>& log,
             int& missing, bool& anyApplied,
             std::vector<LoadedModel::ResolvedChangeTexture>* resolvedOut,
             const std::vector<rs::legacy_pi::AnimationDef>* setAnimationDefs,
             PartChangeTextureCache* partCache) {
    int applied = 0;
    int matCount = static_cast<int>(mesh.materials.size());

    // 読込直後の値は、発動条件のない指定(Always)だけを反映する。条件つきの指定は
    // MaterialRuntime が実行時にスイッチ状態から決める(ChangeTexture の静的焼き込みと同じ規則)。
    for (int id = 0; id < matCount; ++id) {
        mesh.materials[id].useEnvMap = rs::legacy_pi::MaterialFlagStatic(part.envMaps, id);
        mesh.materials[id].useBumpMap = rs::legacy_pi::MaterialFlagStatic(part.bumpMaps, id);
    }
    // 影・抜きの指定も同じ規則。forceTransparent だけは描画順のバケツ(読込時に分類して実行時に
    // 変えない)なので、条件つきでも載っていれば真にする。
    // CastShadow = no(または種別の既定が no)のパーツは、全材質が影を落とさない。
    for (int id = 0; id < matCount; ++id) {
        rs::render::Material& material = mesh.materials[id];
        const bool transparentStatic = rs::legacy_pi::MaterialFlagStatic(part.transparents, id);
        material.forceAlphaTest = rs::legacy_pi::MaterialFlagStatic(part.alphaZeroTests, id);
        material.forceTransparent = rs::legacy_pi::MaterialFlagPossible(part.transparents, id);
        material.castShadow = part.castShadow && !transparentStatic &&
                              !rs::legacy_pi::MaterialFlagStatic(part.noCastShadows, id);
        material.receiveShadow = !transparentStatic &&
                                 !rs::legacy_pi::MaterialFlagStatic(part.noReceiveShadows, id);
    }

    // ChangeAlpha 対象マテリアルは「能力フラグ」を trigger 無視で立てる
    // （値の静的焼込みではなく、後段の半透明パス分類が base alpha=1.0 でも
    // このマテリアルをブレンド対象に含めるための印。ChangeTexture の静的
    // 焼込みバグとは性質が違い、trigger を見ずに立てても安全）。
    for (const rs::legacy_pi::ChangeAlphaCustomizer& ca : part.changeAlphas)
        if (ca.materialId >= 0 && ca.materialId < matCount)
            mesh.materials[ca.materialId].hasAlphaOverride = true;

    // ChangeTexture は宣言順・trigger を無視して mesh.materials[] へ静的に
    // 焼き込むと、Case/If 条件付きの候補が「最後に宣言されたものが常に勝つ」
    // 状態でアップロードされてしまい、実行時に MaterialRuntime がどの Case も
    // active でないと正しく判定しても(そちらは baseMaterials 基準で正しく動く)
    // GPU 側の初期テクスチャが誤った Case のまま戻らなくなるバグになる
    // (車体色のApplySwitchが読込直後に最後方のCaseのまま止まり、スイッチを戻しても
    //  復帰しなくなる)。そのため mesh.materials[]
    // への書き込みは trig.chain が空(Always=常時適用)の候補のみに限定する。
    // 一方、resolvedOut(実行時プリロード対象パス一覧)は Case/If 条件付きの
    // 候補も含め全件解決して埋める(でないと起動時プリロード漏れでフォールバック
    // の毎フレームGPU同期が発生してフリーズする)。
    PartChangeTextureCache localCache;
    PartChangeTextureCache& cache = partCache ? *partCache : localCache;
    const size_t changeTextureCount = part.ChangeTextureCount();
    if (cache.size() != changeTextureCount) cache.resize(changeTextureCount);
    if (resolvedOut) resolvedOut->resize(changeTextureCount);
    part.ForEachChangeTexture([&](size_t ci, const rs::legacy_pi::ChangeTextureRef& ct) {
        if (ct.materialId < 0 || ct.materialId >= matCount) {
            log.push_back("[plugin][warn] ChangeTexture matID " +
                          std::to_string(ct.materialId) + " out of range");
            return;
        }
        if (!cache[ci]) {
            cache[ci] = ResolveChangeTexture(
                ct, rs::legacy_pi::MaterialFlagPossible(part.envMaps, ct.materialId),
                rs::legacy_pi::MaterialFlagPossible(part.bumpMaps, ct.materialId),
                baseDir, log, missing);
        }
        const ResolvedPartChangeTexture& resolved = *cache[ci];
        if (resolvedOut) (*resolvedOut)[ci] = resolved.paths;
        if (resolved.swappable) mesh.materials[ct.materialId].swappableTexture = true;
        if (ct.trig.chain.empty()) { // Always(無条件)のみ静的焼き込みする
            rs::render::Material& mat = mesh.materials[ct.materialId];
            mat.texturePath = resolved.paths.base;
            // 法線/ORMは実行時の使用可否と同じ規則(法線=Env|Bump、ORM=Env)で初期値を絞る。
            mat.normalPath = (mat.useEnvMap || mat.useBumpMap) ? resolved.paths.normal : std::string();
            mat.ormPath = mat.useEnvMap ? resolved.paths.pbr : std::string();
        }
        if (!ct.base.empty() || !ct.normal.empty() || !ct.pbr.empty()) {
            ++applied;
            if (!ct.normal.empty() || !ct.pbr.empty()) anyApplied = true;
        }
    });

    // SetAnimation の画像切替(Simple/Numbered)フレームの対象マテリアルも、
    // 実行時にテクスチャが差し替わる（LED 方向幕は文字テクスチャ↔non2.png(透明)を
    // 点滅させる）。参照先アニメに Simple/Numbered フレームが1つでもあれば印を立てる
    // （UV 型アニメ=SlideUV/TiledUV/RotationUV はテクスチャを差し替えないため対象外）。
    // ChangeTexture 側と違い、こちらはフレームの参照先パスを解決していないため
    // 候補画像の中身では判定せず、宣言の有無で立てたままにする(意図的)。
    if (setAnimationDefs) {
        part.ForEachSetAnimation([&](const rs::legacy_pi::SetAnimationCustomizer& sa) {
            if (sa.materialId < 0 || sa.materialId >= matCount) return;
            for (const rs::legacy_pi::AnimationDef& def : *setAnimationDefs) {
                if (def.name != sa.animationName) continue;
                bool swaps = false;
                for (const rs::legacy_pi::AnimationFrame& fr : def.frames) {
                    if (fr.kind == rs::legacy_pi::AnimationFrame::Kind::Simple ||
                        fr.kind == rs::legacy_pi::AnimationFrame::Kind::Numbered) {
                        swaps = true;
                        break;
                    }
                }
                if (swaps) mesh.materials[sa.materialId].swappableTexture = true;
                break;
            }
        });
    }
    return applied;
}

void BakeModelScale(rs::render::Mesh& mesh, float scale) {
    if (scale == 1.0f) return;
    for (rs::render::MeshVertex& v : mesh.vertices) {
        v.pos[0] *= scale;
        v.pos[1] *= scale;
        v.pos[2] *= scale;
    }
}

std::vector<rs::core::Mat4> CollectStaticPartWorlds(const LoadedPlugin& plugin) {
    std::vector<rs::core::Mat4> perPartDefWorlds(plugin.def.parts.size(),
                                                  rs::core::Mat4::Identity());
    for (size_t i = 0; i < plugin.def.parts.size(); ++i) {
        std::memcpy(perPartDefWorlds[i].m, plugin.def.parts[i].worldMat,
                    sizeof(perPartDefWorlds[i].m));
    }
    return GatherAssemblyPartWorlds(plugin, perPartDefWorlds);
}

std::vector<rs::core::Mat4> GatherAssemblyPartWorlds(
    const LoadedPlugin& plugin, const std::vector<rs::core::Mat4>& perPartDefWorlds) {
    std::vector<rs::core::Mat4> partWorlds(plugin.assemblyPlacements.size(),
                                           rs::core::Mat4::Identity());
    for (size_t i = 0; i < plugin.assemblyPlacements.size(); ++i) {
        const int partDefIndex = plugin.assemblyPlacements[i].WorldPartIndex();
        if (partDefIndex < 0 || static_cast<size_t>(partDefIndex) >= perPartDefWorlds.size()) continue;
        partWorlds[i] = perPartDefWorlds[static_cast<size_t>(partDefIndex)];
    }
    return partWorlds;
}

static void TransformBakedPosition(const float pos[3], const rs::core::Mat4& m, float out[3]) {
    out[0] = pos[0] * m.m[0] + pos[1] * m.m[4] + pos[2] * m.m[8] + m.m[12];
    out[1] = pos[0] * m.m[1] + pos[1] * m.m[5] + pos[2] * m.m[9] + m.m[13];
    out[2] = pos[0] * m.m[2] + pos[1] * m.m[6] + pos[2] * m.m[10] + m.m[14];
}

void ComputeBakedAabb(const rs::render::Mesh& mesh, const std::vector<rs::core::Mat4>& partWorlds,
                      float outAabbMin[3], float outAabbMax[3], size_t* outSkippedVertexCount) {
    size_t skipped = 0;
    bool first = true;
    for (int axis = 0; axis < 3; ++axis) outAabbMin[axis] = outAabbMax[axis] = 0.0f;
    for (const rs::render::MeshVertex& v : mesh.vertices) {
        float baked[3] = {v.pos[0], v.pos[1], v.pos[2]};
        if (v.partIndex < partWorlds.size()) TransformBakedPosition(v.pos, partWorlds[v.partIndex], baked);
        else ++skipped;
        for (int axis = 0; axis < 3; ++axis) {
            if (first || baked[axis] < outAabbMin[axis]) outAabbMin[axis] = baked[axis];
            if (first || baked[axis] > outAabbMax[axis]) outAabbMax[axis] = baked[axis];
        }
        first = false;
    }
    if (outSkippedVertexCount) *outSkippedVertexCount = skipped;
}

void BakeStaticPartWorlds(rs::render::Mesh& mesh, const std::vector<rs::core::Mat4>& partWorlds,
                          size_t* outSkippedVertexCount) {
    size_t skipped = 0;
    for (rs::render::MeshVertex& v : mesh.vertices) {
        if (v.partIndex >= partWorlds.size()) { ++skipped; continue; }
        const rs::core::Mat4& m = partWorlds[v.partIndex];

        float baked[3];
        TransformBakedPosition(v.pos, m, baked);
        v.pos[0] = baked[0]; v.pos[1] = baked[1]; v.pos[2] = baked[2];

        const float nx = v.normal[0] * m.m[0] + v.normal[1] * m.m[4] + v.normal[2] * m.m[8];
        const float ny = v.normal[0] * m.m[1] + v.normal[1] * m.m[5] + v.normal[2] * m.m[9];
        const float nz = v.normal[0] * m.m[2] + v.normal[1] * m.m[6] + v.normal[2] * m.m[10];
        v.normal[0] = nx; v.normal[1] = ny; v.normal[2] = nz;
        rs::core::Normalize3(v.normal);
        // パーツ行列は焼き込み済みなので、頂点を1パーツ(partIndex=0)にそろえる。
        // GPUインスタンス描画は gPartWorld[instanceId * partsPerInstance + partIndex] を引くため、
        // 添字が残るとパーツ数1のバッチでは別の配置の行列を引いて、そのパーツが消える・ずれる。
        v.partIndex = 0;
    }
    if (outSkippedVertexCount) *outSkippedVertexCount = skipped;
    mesh.ComputeAabb();
}

} // namespace rs::legacy_plugin
