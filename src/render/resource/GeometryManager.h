#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <ResourceUploadBatch.h>

#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <string>

#include "../mesh/Mesh.h"
#include "GeometryResource.h"
#include "WeakPtrCache.h"

namespace rs::render {

// 頂点/インデックスバッファの basePath 単位 dedup を担う。TextureManager と同じ
// パターン(weak_ptr キャッシュ+mutex)。複数の配置(MeshPipeline のシーン常駐
// スロット/InstanceBatch)から同一モデルが要求された場合、GPU 頂点/インデックス
// バッファを1回だけ DEFAULT ヒープへアップロードし shared_ptr で共有する。
// SRV 等の descriptor 配置はここに含めない(呼び出し側の責務)。
class GeometryManager {
public:
    // basePath をキーに共有ジオメトリを返す。未生成なら mesh の頂点/インデックスを
    // DEFAULT ヒープへアップロードする。mesh が空(頂点/インデックスどちらか空)、
    // または GPU リソース生成に失敗した場合は nullptr(呼び出し側で弾くこと)。
    std::shared_ptr<GeometryResource> Acquire(
        ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
        const std::string& basePath, const Mesh& mesh);

    // 使われなくなったジオメトリを、上限の範囲で残す。視点距離のモデル切替は同じモデルを
    // 行き来するので、残しておけば Acquire がGPUバッファを作り直さずに返せる。
    // 残している間は同じキーの Acquire が古い内容を返すので、キーと内容が1対1の経路だけが呼ぶ。
    void KeepRecent(const std::shared_ptr<GeometryResource>& resource);
    void ClearRecent();

private:
    mutable std::mutex                          mutex_;
    WeakPtrCache<std::string, GeometryResource> cache_;
    std::list<std::shared_ptr<GeometryResource>> recent_; // 先頭が最後に残したもの
    size_t                                      recentBytes_ = 0;
};

GeometryManager& GetGeometryManager();

} // namespace rs::render
