// MeshTexturePool.cpp - MeshPipeline から切り出した永続テクスチャプール管理
// (GetOrCreateTexGroup/PreloadTexture/EnsureDummyTextureTable)。UploadMesh
// (初回ロード)と実行時のChangeTexture/SetAnimation事前ロードの両方が経由する
// 共通経路。
#include "MeshTexturePool.h"

#include <spdlog/spdlog.h>

#include "../resource/TextureLoader.h"
#include "../resource/TextureManager.h"
#include "../resource/TextureUpload.h"

namespace rs::render {

void MeshTexturePool::Init(ID3D12DescriptorHeap* srvHeap, UINT srvSize,
                           UINT firstTextureDescriptorIndex, UINT maxTextures,
                           TextureManager& textureManager) {
    srvHeap_ = srvHeap;
    textureManager_ = &textureManager;
    srvSize_ = srvSize;
    firstTexIndex_ = firstTextureDescriptorIndex;
    maxTextures_ = maxTextures;
}

void MeshTexturePool::EnsureDummyTextureTable(ID3D12Device* device,
                                              DirectX::ResourceUploadBatch& upload) {
    // 先頭3 slot は「テクスチャ無しマテリアル用」のダミー table（白/平坦法線/
    // 既定ORM）。ルート descriptor table は常に有効な3 descriptor を指す必要がある。
    // テクスチャプールと同様に切替を跨いで保持するため、未設定時のみ書く。
    // GPUリソースは TextureManager が全パイプラインで共有する1組(スロットごとに
    // 作ると、配置の多いレイアウトでテクスチャ生成が読込時間の大きな割合を占める)。
    if (!dummy_) {
        std::shared_ptr<TextureResource> dummy = textureManager_->AcquireDefaultTextures(device, upload);
        if (!dummy->base || !dummy->normal || !dummy->orm) return;
        WriteTextureSrv(device, srvHeap_, srvSize_, dummy->base.Get(), firstTexIndex_ + 0, true);
        WriteTextureSrv(device, srvHeap_, srvSize_, dummy->normal.Get(), firstTexIndex_ + 1, false);
        WriteTextureSrv(device, srvHeap_, srvSize_, dummy->orm.Get(), firstTexIndex_ + 2, false);
        dummy_ = std::move(dummy);
        nextTexSlot_ = 3;
    }
}

// (base, normal, pbr)の3つ組をキーに永続テクスチャプールを引く。未登録なら TextureManager
// から GPU リソース(base/normal/orm)を取得し、このパイプライン固有の空きスロットへ
// SRV を配置して登録する。UploadMesh(ベースメッシュ)と PreloadTexture
// (ChangeTexture/SetAnimation候補)の共通経路。GPU リソース本体は TextureManager が
// 同じ3つ組キーでパイプライン横断に生存管理する(同一組の二重アップロード防止)。
// texPool_ はこのパイプラインでの shared_ptr 強参照+スロット番号のみを持つ。
//
// スロット確保数: normal/orm を持たない登録(normalPath/pbrPathともに空)は
// base の1スロットのみ確保する。シェーダは useNormal/useOrm(gUseTexture.y/.z、
// MatData.flags.x/.y)が真のときしか texSlotOffset+1/+2 を読まず、そのフラグは
// 呼び出し側のパス文字列ではなく本関数が返す PooledTexGroup(hasNormal/hasOrm)
// から導出させる。
//
// キーに normal/pbr を含めるのは、法線/ORMの宣言が同じ base でもマテリアルや
// ChangeTexture候補ごとにまちまちなため。base 単独キーだと、先に登録された1スロット
// (法線/ORMなし)が後続の法線/ORM付き要求を黙って打ち消してしまう(条件付き
// ChangeTexture は静的に焼き込まれずメッシュ登録時は法線/ORMなしになるため、PBR指定の
// プラグインで実際に起きた)。3つ組なら登録順に結果が依らず、呼び出し側でのunionは要らない。
MeshTexturePool::PooledTexGroup* MeshTexturePool::GetOrCreateTexGroup(
    ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
    const std::string& basePath, const std::string& normalPath,
    const std::string& pbrPath) {
    if (basePath.empty()) return nullptr;

    const TextureCacheKey key(basePath, normalPath, pbrPath);
    std::unordered_map<TextureCacheKey, PooledTexGroup, TextureCacheKeyHash>::iterator it =
        texPool_.find(key);
    if (it != texPool_.end()) return &it->second; // 常駐済み

    const UINT need = TextureSlotCost(normalPath, pbrPath);

    if (nextTexSlot_ + need > maxTextures_) {
        // 枠切れはこのパイプラインでは回復しない(nextTexSlot_はDiscardTexturePool()まで
        // 減らない)。記録して以後の同期GPU再試行を呼び出し側で止められるようにする。
        // 警告は同じパスで何度も出しても意味が無いので初回のみ。3スロット要求が
        // 失敗しても、後続の1スロット要求(normal/orm無し)は入りうるため、この
        // 失敗フラグはあくまで3つ組単位のもの(パイプライン全体の恒久失敗
        // ではない)として texBudgetFailed_ に留める。
        if (texBudgetFailed_.insert(key).second) {
            spdlog::warn("[mesh] texture slot budget exhausted (maxTextures={}, need={}): {}",
                         maxTextures_, need, basePath);
        }
        return nullptr;
    }

    // 読込/GPU生成の恒久失敗判定・再試行の停止・失敗プレースホルダーの生成は
    // TextureManager が basePath 単位で一元管理する(戻り値が nullptr になることはない)。
    std::shared_ptr<TextureResource> resource = textureManager_->Acquire(
        device, upload, basePath, normalPath, pbrPath);

    UINT baseSlot = firstTexIndex_ + nextTexSlot_;
    WriteTextureSrv(device, srvHeap_, srvSize_, resource->base.Get(), baseSlot, true);
    if (need == 3) {
        WriteTextureSrv(device, srvHeap_, srvSize_, resource->normal.Get(), baseSlot + 1, false);
        WriteTextureSrv(device, srvHeap_, srvSize_, resource->orm.Get(), baseSlot + 2, false);
    }

    PooledTexGroup group;
    group.resource = std::move(resource);
    group.texSlotOffset = nextTexSlot_; // ダミーは0、実テクスチャは3以降(相対スロット添字)
    // フラグは「このパイプラインで実際にSRVを書いたか」で決める。resource側の
    // hasNormal/hasOrm をそのまま使ってはならない: TextureManager のキャッシュも
    // basePath 単体キーなので、normal付きで先に常駐した同一baseをここで
    // base のみ(need==1)で要求すると hasNormal=true のリソースが返り、+1スロット
    // (=隣のテクスチャのbase)を法線として読む事故になる。
    group.hasNormal = (need == 3) && group.resource->hasNormal;
    group.hasOrm = (need == 3) && group.resource->hasOrm;
    group.slotCount = need;
    nextTexSlot_ += need;

    std::pair<std::unordered_map<TextureCacheKey, PooledTexGroup, TextureCacheKeyHash>::iterator, bool> ins =
        texPool_.emplace(key, std::move(group));
    return &ins.first->second;
}

// ChangeTexture/SetAnimation の実行時反映用にテクスチャを事前ロードする。
// 実体は GetOrCreateTexGroup(永続プール)。同一 base は再ロードせず登録済みの
// texSlotOffset/hasNormal/hasOrm(グループ側から導出、呼び出し側のパス文字列
// からは判定しない)を返す。
bool MeshTexturePool::PreloadTexture(
    ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
    const std::string& basePath, const std::string& normalPath,
    const std::string& pbrPath, PreloadedTexInfo& infoOut) {
    PooledTexGroup* g = GetOrCreateTexGroup(device, upload, basePath, normalPath,
                                            pbrPath);
    if (!g) return false;
    infoOut.texSlotOffset = g->texSlotOffset;
    infoOut.hasNormal = g->hasNormal;
    infoOut.hasOrm = g->hasOrm;
    infoOut.hasAlpha = g->resource->hasAlpha;
    infoOut.hasZeroAlpha = g->resource->hasZeroAlpha;
    infoOut.semiAlphaFraction = g->resource->semiAlphaFraction;
    return true;
}

bool MeshTexturePool::FindPreloadedSlot(const std::string& basePath, const std::string& normalPath,
                                        const std::string& pbrPath, PreloadedTexInfo& infoOut) const {
    std::unordered_map<TextureCacheKey, PooledTexGroup, TextureCacheKeyHash>::const_iterator it =
        texPool_.find(TextureCacheKey(basePath, normalPath, pbrPath));
    if (it == texPool_.end()) return false;
    infoOut.texSlotOffset = it->second.texSlotOffset;
    infoOut.hasNormal = it->second.hasNormal;
    infoOut.hasOrm = it->second.hasOrm;
    infoOut.hasAlpha = it->second.resource->hasAlpha;
    infoOut.hasZeroAlpha = it->second.resource->hasZeroAlpha;
    infoOut.semiAlphaFraction = it->second.resource->semiAlphaFraction;
    return true;
}

void MeshTexturePool::ApplyInstanceMaterialTexture(InstanceMatOverride& value, bool hasTexture,
                                                   const PreloadedTexInfo& info) const {
    value.flags[0] = hasTexture && info.hasNormal ? 1.0f : 0.0f;
    value.flags[1] = hasTexture && info.hasOrm ? 1.0f : 0.0f;
    value.tex[0] = AbsoluteTexIndex(hasTexture ? info.texSlotOffset : 0);
    value.tex[1] = hasTexture ? 1u : 0u;
}

} // namespace rs::render
