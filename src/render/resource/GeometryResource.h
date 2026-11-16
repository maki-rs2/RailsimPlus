#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

namespace rs::render {

// GPU 常駐済みの共有ジオメトリ(頂点/インデックスバッファ、DEFAULT ヒープ配置)。
// 生成は GeometryManager::Acquire() のみが行い、basePath 単位で複数の
// MeshPipeline(シーン常駐スロット)/InstanceBatch から shared_ptr で共有される
// (同一モデルの GPU 頂点/インデックス二重アップロード防止)。
struct GeometryResource {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
    uint32_t                               vertexStride = 0;
    uint32_t                               vertexCount = 0;
    uint32_t                               indexCount = 0;
};

} // namespace rs::render
