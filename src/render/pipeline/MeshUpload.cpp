// MeshUpload.cpp - MeshPipeline::UploadMesh(頂点/インデックス/マテリアル
// テクスチャのGPUアップロード、半透明分類、半透明三角形の構造化バッファ準備)。
// テクスチャプール管理はMeshTexturePool.cpp、実行時マテリアル更新は
// MeshMaterialRuntime.cppへ分離。
#include "MeshPipeline.h"
#include "../../platform/EnvironmentVariable.h"
#include "SubmeshAlphaClassify.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "../resource/TextureLoader.h"
#include "../resource/AsyncLoader.h"
#include "../resource/TextureManager.h"
#include "../resource/TextureUpload.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

void MeshPipeline::UploadMaterialTextures(ID3D12Device* device,
                                          DirectX::ResourceUploadBatch& upload,
                                          double& profTexMsOut) {
    using ProfClock = std::chrono::steady_clock;

    // 各マテリアルの (base,normal,orm) を永続テクスチャプール(texPool_)へ登録/参照する。
    //
    // アセンブリメッシュは全パーツのマテリアルを連結するため、同一 base テクスチャが
    // 多数のマテリアルに重複して現れる（実データの駅プラグインで実体10組が276
    // マテリアル参照）。さらにモデル切替(ChangeModel)は毎回 UploadMesh を呼ぶが、
    // テクスチャはバリアント間でほぼ不変。プールを切替間で保持することで、同一 base は
    // デコードコピー/GPUリソース生成/アップロード/alpha走査を初回1回だけ行い、以降
    // (同一メッシュ内の重複＋切替後の再アップロード)はスロット参照のみで済む
    // (VRAM・アップロード帯域・切替ヒッチを大幅削減)。ランタイムのテクスチャ差替
    // (ChangeTexture/画像切替アニメ)は UpdateMaterialTexture が texSlotOffset(ひいては
    // ResourceDescriptorHeap[]添字)を差し替えるだけで既存スロットの SRV/リソースを
    // 上書きしないため、プール共有は安全。
    ProfClock::time_point profTex0 = ProfClock::now();
    size_t prefetchCount = 0;

    // 非同期ロード基盤の一部: 登録時にデコードが要る
    // (未常駐・未失敗で、他パイプラインが作ったGPUリソースも無い)
    // basePath/normalPath/pbrPathをまとめてワーカースレッドへ並列デコード投入し、
    // ImageCacheを温める。GPUリソースを共有できる組は画像を読まないので、先読みすると
    // 画像のコピー(LRUから追い出されていれば再デコード)が無駄になる。テクスチャ処理時間の
    // 大半はCPUデコードなので、GPUアップロード自体は同期経路のまま
    // デコードだけを並列化する。以降のループは温まったキャッシュにヒットするので、
    // 実質のデコードコストが下がる。
    {
        std::vector<std::string> prefetchPaths;
        prefetchPaths.reserve(materialSet_.Count() * 3);
        for (const Material& mat : materialSet_.Materials()) {
            if (mat.texturePath.empty()) continue;
            if (!texturePool_.NeedsImageDecode(mat.texturePath, mat.normalPath, mat.ormPath)) continue;
            prefetchPaths.push_back(mat.texturePath);
            if (!mat.normalPath.empty()) prefetchPaths.push_back(mat.normalPath);
            if (!mat.ormPath.empty()) prefetchPaths.push_back(mat.ormPath);
        }
        PrefetchImages(prefetchPaths);
        prefetchCount = prefetchPaths.size();
    }
    const ProfClock::time_point profLoop0 = ProfClock::now();

    size_t texRefCount = 0;   // テクスチャ参照を持つマテリアル数
    size_t texReuseCount = 0; // プール常駐(再ロード/アップロード無し)で済んだマテリアル数
    for (size_t m = 0; m < materialSet_.Count(); ++m) {
        const Material& mat = materialSet_.Materials()[m];
        if (mat.texturePath.empty()) continue;
        ++texRefCount;

        bool alreadyResident = texturePool_.IsResident(mat.texturePath, mat.normalPath, mat.ormPath);
        MeshTexturePool::PooledTexGroup* g = texturePool_.GetOrCreateTexGroup(
            device, upload, mat.texturePath, mat.normalPath, mat.ormPath);
        if (!g) continue; // 読込失敗/スロット上限 → テクスチャ無し扱い
        if (alreadyResident) ++texReuseCount;

        GpuTexture& texture = materialSet_.MutableTextures()[m];
        texture.resource = g->resource->base;
        texture.normalRes = g->resource->normal;
        texture.ormRes = g->resource->orm;
        texture.valid = true;
        // hasNormal/hasOrmはg->resource(TextureManagerのbasePath単体キー生フラグ)
        // ではなくg(このパイプラインでの登録時にneed==3から導出済みの値)を使う。
        // 生フラグを使うと、同一baseを他パイプラインがnormal/orm付きで先に常駐
        // させていた場合、このパイプラインでは1スロットしか確保していないのに
        // hasNormal/hasOrm=trueが立ち、隣接スロット(=別テクスチャのbaseスロット)を
        // 法線/ORMとして誤読する。
        texture.hasNormal = g->hasNormal;
        texture.hasOrm = g->hasOrm;
        texture.hasAlpha = g->resource->hasAlpha;
        texture.hasZeroAlpha = g->resource->hasZeroAlpha;
        texture.semiAlphaFraction = g->resource->semiAlphaFraction;
        texture.texSlotOffset = g->texSlotOffset;
    }

    if (texReuseCount > 0) {
        // SceneRenderer::Sync等でエンティティ数分呼ばれることがあるため、
        // 常時出力すると件数が多いレイアウトでログ自体が負荷になる。infoではなく
        // debugにする(診断に役立つので、削除はしない)。
        spdlog::debug("[mesh] texture pool: {} refs -> {} newly uploaded "
                     "({} served from resident pool)",
                     texRefCount, texRefCount - texReuseCount, texReuseCount);
    }
    const ProfClock::time_point profEnd = ProfClock::now();
    profTexMsOut = std::chrono::duration<double, std::milli>(profEnd - profTex0).count();
    // 並列デコード(prefetch)とGPUテクスチャ生成ループの比率。打ち手が分かれるため
    // 内訳を出す(計測専用、RS_PROFILE_LOAD指定時のみ)。
    if (rs::platform::ReadEnvironmentVariable("RS_PROFILE_LOAD") != nullptr && texRefCount > 0) {
        spdlog::info("[profile] meshTex prefetch(並列デコード)={:.0f}ms(x{}) "
                     "gpuTexLoop={:.0f}ms(refs={} new={})",
                     std::chrono::duration<double, std::milli>(profLoop0 - profTex0).count(),
                     prefetchCount,
                     std::chrono::duration<double, std::milli>(profEnd - profLoop0).count(),
                     texRefCount, texRefCount - texReuseCount);
    }
}

void MeshPipeline::ComputeSubmeshCentersAndTransparentTris(const Mesh& mesh) {
    // 近似中心（頂点平均）と半透明三角形リストを計算してキャッシュ
    for (SubMesh& sm : geometry_.Submeshes()) {
        double cx = 0, cy = 0, cz = 0;
        uint32_t n = 0;
        for (uint32_t k = 0; k < sm.indexCount; ++k) {
            uint32_t idx = mesh.indices[sm.indexOffset + k];
            if (idx >= mesh.vertices.size()) continue;
            const float (&p)[3] = mesh.vertices[idx].pos;
            cx += p[0]; cy += p[1]; cz += p[2];
            ++n;
        }
        if (n > 0) {
            sm.center[0] = float(cx / n);
            sm.center[1] = float(cy / n);
            sm.center[2] = float(cz / n);
        }

        // 半透明サブメッシュは三角形単位でも重心を記録し、描画時に細かくソートする
        if (sm.IsTransparent()) {
            for (uint32_t k = 0; k + 2 < sm.indexCount; k += 3) {
                uint32_t i0 = mesh.indices[sm.indexOffset + k];
                uint32_t i1 = mesh.indices[sm.indexOffset + k + 1];
                uint32_t i2 = mesh.indices[sm.indexOffset + k + 2];
                if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() ||
                    i2 >= mesh.vertices.size())
                    continue;
                const float (&p0)[3] = mesh.vertices[i0].pos;
                const float (&p1)[3] = mesh.vertices[i1].pos;
                const float (&p2)[3] = mesh.vertices[i2].pos;
                TransparentTri tri;
                tri.materialIndex = sm.materialIndex;
                tri.idx[0] = i0;
                tri.idx[1] = i1;
                tri.idx[2] = i2;
                tri.partIndex = mesh.vertices[i0].partIndex;
                tri.center[0] = (p0[0] + p1[0] + p2[0]) / 3.0f;
                tri.center[1] = (p0[1] + p1[1] + p2[1]) / 3.0f;
                tri.center[2] = (p0[2] + p1[2] + p2[2]) / 3.0f;
                transparency_.MutableTris().push_back(tri);
            }
        }
    }
}

// 全マテリアルの MatData(CPU配列)を構築する。半透明三角形の描画(WBOIT)だけでなく、
// TrainInstanceBatch::MaterialData()経由でコンシスト個体差(ドア開閉等の
// per-instanceオーバーライド)のベース値としても参照される(SceneMaterialOverrides.cpp
// ResolveSceneStaticMaterialOverrides/RestoreBaseTextureIndices)。後者は半透明
// submeshの有無と無関係にmaterialIndexで引くため、半透明が0件のジオメトリでも
// 必ず構築しておく必要がある(半透明三角形が無いバッチだけ
// このCPU配列が未構築のまま空になると、動的マテリアルのオーバーライドが全滅して
// デフォルト値(絶対テクスチャ添字0=無関係な別テクスチャ)にフォールバックし、
// そのバッチに属する編成全体が別テクスチャで描画される)。
void MeshPipeline::BuildMatDataCpu() {
    // 全マテリアル定数を CPU 配列に一本化して構築（HLSL の MatData と
    // float4 単位で一致）。UpdateMaterialColor/UpdateMaterialTexture は
    // このCPU配列のみを書き換え、GPU 側バッファへは Record() が
    // フレームごとに memcpy で反映する（torn write によるデバイスロスト対策）。
    UINT matCount = UINT(materialSet_.Count());
    if (matCount == 0) {
        // マテリアルが無い半透明はありえないが、gMats が空だと SRV を張れない
        // ため、ダミー1件を確保しておく。
        matCount = 1;
    }
    std::vector<MatData>& matDataCpu = transparency_.MutableMatDataCpu();
    matDataCpu.assign(matCount, MatData{});
    for (size_t m = 0; m < materialSet_.Count(); ++m) {
        const Material& src = materialSet_.Materials()[m];
        MatData& d = matDataCpu[m];
        ApplyMaterialColorAndUvTransformToMatData(src, d);
        const GpuTexture& gt = materialSet_.Textures()[m];
        const bool hasTex = gt.valid;
        d.flags[0] = (hasTex && gt.hasNormal) ? 1.0f : 0.0f;
        d.flags[1] = (hasTex && gt.hasOrm) ? 1.0f : 0.0f;
        d.tex[0] = texturePool_.AbsoluteTexIndex(hasTex ? gt.texSlotOffset : 0u); // テクスチャ無しはダミー(texSlotOffset=0)
        d.tex[1] = hasTex ? 1u : 0u;           // useBase
    }
}

bool MeshPipeline::UploadTransparentBuffers() {
    // 半透明三角形があれば、フレームごとに書き換える動的インデックスバッファと
    // per-triangle マテリアル番号バッファを用意する。全マテリアルのMatData自体は
    // BuildMatDataCpu()が(半透明の有無に関わらず)既に構築済みの前提。
    spdlog::debug("[mesh] transparent tris: {}", transparency_.Tris().size());
    // transparentTris_ が入れ替わったので両フレームの動的IBを要再構築にする
    // (RecordTransparent/RecordTransparentInstanced が次回描画時に埋め直す)。
    transparency_.MarkAllContentDirty();
    return transparency_.UploadGpuBuffers();
}

bool MeshPipeline::UploadMesh(
    ID3D12Device* device, DirectX::ResourceUploadBatch& upload, const Mesh& mesh,
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& keepAlive,
    const std::string& geometryKey) {
    // [profile] 計測専用(環境変数 RS_PROFILE_LOAD 指定時のみ)。モデル切替のヒッチ
    // 内訳(テクスチャ再構築 vs ジオメトリ)を掴むため record 側の時間を測る。
    using ProfClock = std::chrono::steady_clock;
    ProfClock::time_point profT0 = ProfClock::now();
    double profTexMs = 0.0;

    // ジオメトリ側のみ破棄。テクスチャプール(texturePool_)は保持し、モデル切替で
    // 不変なテクスチャの GPU 再構築を丸ごと省く。
    ResetGeometry();
    geometryKey_ = geometryKey;
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        spdlog::warn("[mesh_pipeline] UploadMesh: 空メッシュのためアップロード中止 key={} "
                    "verts={} indices={}",
                    geometryKey, mesh.vertices.size(), mesh.indices.size());
        return false;
    }

    geometry_.Submeshes() = mesh.submeshes;

    materialSet_.Assign(mesh.materials);

    texturePool_.EnsureDummyTextureTable(device, upload);
    UploadMaterialTextures(device, upload, profTexMs);

    // ここまでで materialSet_ の textures は「ロード直後(=trig.trigger==Alwaysなgeometryのみ
    // 反映済み。ApplyPbrがCase/If条件付きChangeTextureを静的に焼き込まなくなった
    // ため、これがそのまま真の.xベース状態になる)」を表す。実行時に
    // MaterialRuntime のどの Case/条件も active でなくなった際、
    // RevertMaterialTexture() でここへ明示的に戻すためのスナップショットを取る。
    materialSet_.SnapshotBase();

    ClassifySubmeshTransparency();

    // submesh定数バッファ(cbUpload_)は実際に必要な枠数(submesh数+cutout submesh数+2、
    // cutout submeshは深度専用パス分としてさらに1枠を使い、末尾2枠は
    // RecordTransparent系のbase定数・RecordHighlightSelfのティント定数用の予約)
    // だけ確保する。gPartWorld側(EnsurePartWorldCapacityをframeIndex単位で呼び、
    // もう片方のフレームは次回描画時に追従)と異なり、メッシュ差し替えの都度
    // submesh数が不連続に変わるため、ここでkFrames両方を即座に育てる。
    // **ClassifySubmeshTransparency()より後で数えること**: alphaClassを設定するのは
    // その関数だけ(SubMeshの初期分類はOpaque)なので、前で数えると常に0になり、
    // 枠不足のまま描画時にcbLimitを超えた分がサイレントに抜ける。
    // 2段(深度専用パス→カラーパス)で描くsubmeshを数える。cutoutだけでなく
    // lateOpaqueと「不透明かつ中間alphaテクスチャ」も2回描くため全て含める。
    UINT twoPassSubmeshCount = 0;
    for (const SubMesh& submesh : geometry_.Submeshes()) {
        if (submesh.NeedsTwoPassDraw()) ++twoPassSubmeshCount;
    }
    const UINT requiredCbSlots =
        static_cast<UINT>(geometry_.Submeshes().size()) + twoPassSubmeshCount + 2;
    for (UINT f = 0; f < kFrames; ++f) {
        if (!constantBuffer_.EnsureCapacity(device, requiredCbSlots, f)) {
            spdlog::warn("[mesh_pipeline] UploadMesh: 定数バッファ拡張に失敗 key={} required={}",
                         geometryKey, requiredCbSlots);
            return false;
        }
    }
    if (rs::platform::ReadEnvironmentVariable("RS_DUMP_ALPHA_CLASS") != nullptr) {
        spdlog::info("[alphaclass] cbSlots key={} submeshes={} twoPass={} required={}", geometryKey,
                     geometry_.Submeshes().size(), twoPassSubmeshCount, requiredCbSlots);
    }

    if (!geometry_.Upload(device, upload, mesh, keepAlive, geometryKey)) return false;

    ComputeSubmeshCentersAndTransparentTris(mesh);

    BuildMatDataCpu();
    if (!transparency_.Empty()) {
        if (!UploadTransparentBuffers()) return false;
    }

    const double submeshesPerMaterial = materialSet_.Count() == 0
        ? 0.0
        : static_cast<double>(geometry_.Submeshes().size()) / static_cast<double>(materialSet_.Count());
    spdlog::debug("[mesh] upload rawSubmeshes={} materials={} materialAggregationRatio={:.2f}",
                 geometry_.Submeshes().size(), materialSet_.Count(), submeshesPerMaterial);
    if (rs::platform::ReadEnvironmentVariable("RS_PROFILE_LOAD") != nullptr) {
        double totalMs = std::chrono::duration<double, std::milli>(
                             ProfClock::now() - profT0).count();
        spdlog::info("[profile] uploadMesh record total={:.2f}ms tex={:.2f}ms "
                     "geom+other={:.2f}ms (verts={} mats={})",
                     totalMs, profTexMs, totalMs - profTexMs, mesh.vertices.size(),
                     materialSet_.Count());
    }
    return true;
}

} // namespace rs::render
