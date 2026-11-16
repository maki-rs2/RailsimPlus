#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <ResourceUploadBatch.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_set>

#include "TextureResource.h"
#include "WeakPtrCache.h"

namespace rs::render {

// (basePath, normalPath, pbrPath) の正規化済み三つ組。GPU常駐リソースは
// この三つ組単位で共有される(同じ base でも normal/orm の組み合わせが
// 違えば別リソースになるため)。
using TextureCacheKey = std::tuple<std::string, std::string, std::string>;

// TextureCacheKey 用ハッシュ(std::tuple に既定の std::hash 特殊化は無い)。
struct TextureCacheKeyHash {
    std::size_t operator()(const TextureCacheKey& key) const noexcept {
        const std::size_t h0 = std::hash<std::string>{}(std::get<0>(key));
        const std::size_t h1 = std::hash<std::string>{}(std::get<1>(key));
        const std::size_t h2 = std::hash<std::string>{}(std::get<2>(key));
        // boost::hash_combine相当の簡易合成。
        std::size_t seed = h0;
        seed ^= h1 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
};

// テクスチャデコード(TextureLoader::LoadImageRGBA、内部にImageCache)+ GPU
// アップロードの (basePath, normalPath, pbrPath) 単位 dedup を担う。複数の
// MeshPipeline(シーン常駐スロット)から同一の組が要求された場合、GPU
// リソース(base/normal/orm)を1回だけ作成し weak_ptr キャッシュで共有する。
// SRV descriptor の配置(スロット割当)は呼び出し側(MeshPipeline)の責務のまま
// (パイプラインごとに固定 SRV レンジが分かれているため、リソース共有と
// スロット配置を分離する)。
class TextureManager {
public:
    // (basePath, normalPath, pbrPath) の三つ組をキーに GPU 常駐テクスチャ組を
    // 返す。未生成なら decode+upload する。同じ basePath でも normalPath/pbrPath
    // の組み合わせが異なれば別リソースとして扱う。
    // 読込/生成の失敗が確定した basePath は共有プレースホルダー(マゼンタ)を返す
    // (毎回の再試行による GPU 同期ストームを避けるため、失敗は basePath 単位で
    // キャッシュする。normal/orm の失敗はここに含まれない(base画像自体の問題では
    // ないため、base画像が生きている限り正常にリソース化される))。
    // basePath が空の呼び出しは想定しない(呼び出し側
    // で弾くこと)。戻り値が nullptr になることはない。
    std::shared_ptr<TextureResource> Acquire(
        ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
        const std::string& basePath, const std::string& normalPath,
        const std::string& pbrPath);

    // basePath が失敗が確定済みかどうか。失敗は base 画像のデコード/GPU
    // 生成失敗のみを表し(normal/orm の無い・失敗は含まない)、basePath 単位で
    // 判定できる(normalPath/pbrPathの組み合わせに依存しない)。呼び出し側(毎
    // フレームの実行時切替)はこれが true の間、RunUploadCommands(WaitForGpu を
    // 伴う同期GPU操作)を呼ばずスキップすべき。
    bool IsKnownFailed(const std::string& basePath) const;

    // この三つ組の GPU リソースが生存中か。true なら Acquire は画像を読まずに返すので、
    // 先読み(PrefetchImages)の対象から外してよい。期限切れのエントリはここで捨てる。
    bool HasResident(const std::string& basePath, const std::string& normalPath,
                     const std::string& pbrPath);

    // キャッシュのキーと共有プレースホルダーを捨てる。呼び出し側(MeshPipelineなど)が
    // 持つ shared_ptr は生きたままなので、先に全部手放してから呼べばGPUリソースも解放される。
    // 失敗の記録(basePath単位)は残す(同じ失敗画像を何度も読み直さないため)。
    // 1枚ごとに作って捨てる専用の TextureManager(サムネイル撮影)が、期限切れのキーを
    // 溜めないために使う。
    void Clear();

    // テクスチャの無い材質用の1x1の3枚組(白・平らな法線・既定ORM)。全パイプラインで共有し、
    // 未生成なら作る。SRVの配置は呼び出し側(MeshTexturePool)が行う。
    std::shared_ptr<TextureResource> AcquireDefaultTextures(
        ID3D12Device* device, DirectX::ResourceUploadBatch& upload);

private:
    std::shared_ptr<TextureResource> CreatePlaceholder(
        ID3D12Device* device, DirectX::ResourceUploadBatch& upload);

    mutable std::mutex mutex_;
    WeakPtrCache<TextureCacheKey, TextureResource, TextureCacheKeyHash> cache_;
    std::unordered_set<std::string>  failed_; // basePath単位(理由はAcquireのコメント参照)
    std::shared_ptr<TextureResource> placeholder_;
    std::shared_ptr<TextureResource> defaults_; // AcquireDefaultTextures の3枚組
};

TextureManager& GetTextureManager();

} // namespace rs::render
