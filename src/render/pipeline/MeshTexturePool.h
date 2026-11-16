//
// MeshTexturePool.h - MeshPipelineの永続テクスチャプール管理を切り出したクラス
//
// (base, normal, pbr)の3つ組(絶対解決済みパス)をキーに、GPUへ常駐済みのテクスチャを
// 管理する。UploadMesh(初回ロード)と実行時のChangeTexture/SetAnimation事前ロードの
// 両方が経由する共通経路。GPUリソース本体はTextureManagerが同じ3つ組キーで
// パイプライン横断に生存管理し、このクラスは自パイプライン内のshared_ptr強参照+
// スロット番号のみを持つ。同じbaseでも法線/ORMの宣言が違えば別グループになるため、
// 登録の順序に結果が依らない。
//
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <ResourceUploadBatch.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../resource/TextureManager.h"
#include "../resource/TextureResource.h"
#include "MeshPipelineTypes.h" // InstanceMatOverride/PreloadedTexInfo等の共有型を使うため

namespace rs::render {

class MeshTexturePool {
public:
    // srvHeap は shader-visible。firstTextureDescriptorIndex 以降の descriptor を
    // このプールが使う。textureManager はGPUリソースの取得先で、このプールより長く
    // 生きていること(本編は共有の GetTextureManager()、サムネイル撮影は専用のもの)。
    void Init(ID3D12DescriptorHeap* srvHeap, UINT srvSize, UINT firstTextureDescriptorIndex,
              UINT maxTextures, TextureManager& textureManager);

    // basePath が取得先の TextureManager で恒久失敗と判定済みか。
    bool TexKnownFailed(const std::string& basePath) const {
        return textureManager_->IsKnownFailed(basePath);
    }

    // この三つ組を登録するときに画像のデコードが要るか。このプールに常駐済み、
    // 取得先の TextureManager に GPU リソースが生存中(別パイプラインが先に作った)、
    // 恒久失敗のどれかなら画像を読まないので false。先読みの対象を選ぶのに使う。
    bool NeedsImageDecode(const std::string& basePath, const std::string& normalPath,
                          const std::string& pbrPath) const {
        return !IsResident(basePath, normalPath, pbrPath) && !TexKnownFailed(basePath) &&
               !textureManager_->HasResident(basePath, normalPath, pbrPath);
    }

    // 永続テクスチャプール。(base, normal, pbr)→ロード済み1グループ
    // (GPUリソース本体は TextureResource として TextureManager が同じキーで
    // 複数 MeshPipeline 間 dedup する。ここではそれへの shared_ptr とこのパイプライン
    // 上のスロット番号のみを保持)。
    struct PooledTexGroup {
        uint32_t                          texSlotOffset = 0;
        uint32_t                          slotCount = 3; // 確保済みスロット数(1=baseのみ、3=base/normal/orm)
        bool                              hasNormal = false; // resource->hasNormal から設定(呼び出し側では決めない)
        bool                              hasOrm = false;     // resource->hasOrm から設定
        std::shared_ptr<TextureResource> resource;
    };

    // ダミーテクスチャ3枚(白/平坦法線/既定ORM)を未生成時のみ初期化する。
    void EnsureDummyTextureTable(ID3D12Device* device, DirectX::ResourceUploadBatch& upload);

    // (base, normal, pbr)をキーにプールを引き、未登録なら base/normal/orm を空きスロットへ
    // ロード＋アップロードして登録し、そのグループを返す。base 空/読込失敗/スロット
    // 上限超過は nullptr。ステージングバッファの寿命は upload(ResourceUploadBatch)が
    // 内部で管理する。
    PooledTexGroup* GetOrCreateTexGroup(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                                        const std::string& basePath, const std::string& normalPath,
                                        const std::string& pbrPath);

    // ChangeTexture/SetAnimation の実行時反映用にテクスチャを事前ロードする。
    // 実体は GetOrCreateTexGroup(永続プール)。同一3つ組は再ロードせず登録済みの
    // texSlotOffset/hasNormal/hasOrm(グループ側から導出、呼び出し側のパス文字列
    // からは判定しない)を返す。
    bool PreloadTexture(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                        const std::string& basePath, const std::string& normalPath,
                        const std::string& pbrPath, PreloadedTexInfo& infoOut);

    // PreloadTexture 済みかどうかをコマンドリスト無しで調べる（毎フレームの
    // 実行時切替で GPU コマンド発行を避けるためのキャッシュ参照）。
    bool FindPreloadedSlot(const std::string& basePath, const std::string& normalPath,
                           const std::string& pbrPath, PreloadedTexInfo& infoOut) const;

    void ApplyInstanceMaterialTexture(InstanceMatOverride& value, bool hasTexture,
                                      const PreloadedTexInfo& info) const;

    // この3つ組がこのパイプラインのテクスチャ枠(maxTextures_)超過で登録できないと
    // 判明済みかどうか。
    bool TexBudgetExhausted(const std::string& basePath, const std::string& normalPath,
                            const std::string& pbrPath) const {
        return texBudgetFailed_.find(TextureCacheKey(basePath, normalPath, pbrPath)) !=
               texBudgetFailed_.end();
    }

    // テクスチャ枠の残り数。texPool_ を保持したままジオメトリだけ差し替える
    // (同一プラグインのスイッチ切替)場合、nextTexSlot_ は進む一方で戻らないため、
    // 呼び出し側はこれを見て「まだ保持したまま差し替えてよいか / いったん
    // DiscardTexturePool して枠を巻き戻すか」を判断する。
    UINT RemainingTextureSlots() const {
        return nextTexSlot_ < maxTextures_ ? maxTextures_ - nextTexSlot_ : 0;
    }

    // texSlotOffset(パイプライン内相対スロット添字。baseの位置。+1=normal、
    // +2=ormがあれば続く)から、シェーダの ResourceDescriptorHeap[] が直接参照する
    // 絶対ヒープ添字を求める。texSlotOffset=0は常にダミー(白/平坦法線/既定ORM、
    // UploadMesh()で必ず先頭3スロットに常駐)を指す。
    uint32_t AbsoluteTexIndex(uint32_t texSlotOffset) const {
        return firstTexIndex_ + texSlotOffset;
    }

    // この3つ組が既にプールへ常駐済みか(GetOrCreateTexGroupを呼ばずに存在確認だけ
    // したい呼び出し側向け)。
    bool IsResident(const std::string& basePath, const std::string& normalPath,
                    const std::string& pbrPath) const {
        return texPool_.find(TextureCacheKey(basePath, normalPath, pbrPath)) != texPool_.end();
    }

    // 永続テクスチャプールとダミー table を破棄し、スロット割当を先頭へ戻す。
    // プラグイン切替時(別プラグインのテクスチャで VRAM を専有し続けないよう)に
    // 呼ぶ。プール key は絶対解決済みパスのためプラグイン跨ぎの誤共有は無いが、
    // VRAM を有界化するために明示破棄する。
    void DiscardTexturePool() {
        texPool_.clear();
        texBudgetFailed_.clear();
        nextTexSlot_ = 0;
        dummy_.reset();
    }

private:
    ID3D12DescriptorHeap* srvHeap_ = nullptr;
    TextureManager*       textureManager_ = nullptr;
    UINT                  srvSize_ = 0;
    UINT                  firstTexIndex_ = 0;
    UINT                  maxTextures_ = 0;
    UINT                  nextTexSlot_ = 0;

    // テクスチャ無しマテリアル用ダミー table（白/平坦法線/既定ORM）
    // 3枚とも Record() から毎フレーム参照されうるため、破棄されないよう保持し続ける
    // (実体は TextureManager::AcquireDefaultTextures の共有の1組)。
    std::shared_ptr<TextureResource> dummy_;

    std::unordered_map<TextureCacheKey, PooledTexGroup, TextureCacheKeyHash> texPool_;
    // maxTextures_ の枠切れで登録できなかった3つ組。nextTexSlot_ は
    // DiscardTexturePool() まで減らないため、一度枠切れしたパスはこのパイプラインでは
    // ずっと登録できない。記録しておかないと、毎フレームの実行時切替が
    // 「未登録 → RunUploadCommands(WaitForGpu×2) → また枠切れ」を延々と繰り返し、
    // GPU同期ストーム(TDR)になる。
    std::unordered_set<TextureCacheKey, TextureCacheKeyHash> texBudgetFailed_;
};

} // namespace rs::render
