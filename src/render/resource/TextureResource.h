#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include "LoadState.h"

namespace rs::render {

// GPU 常駐済みの1マテリアル分テクスチャ(base/normal/orm の3枚組)。
// 生成は TextureManager::Acquire() のみが行い、basePath 単位で複数の
// MeshPipeline(シーン常駐スロット)から shared_ptr で共有される
// (同一テクスチャの GPU 二重アップロード防止)。SRV descriptor の配置(スロット
// 割当)はここに含めない(パイプラインごとに固定 SRV レンジが分かれているため
// 呼び出し側の責務)。
struct TextureResource {
    LoadState                              state = LoadState::kNotLoaded;
    Microsoft::WRL::ComPtr<ID3D12Resource> base;
    Microsoft::WRL::ComPtr<ID3D12Resource> normal;
    Microsoft::WRL::ComPtr<ID3D12Resource> orm;
    bool                                   hasNormal = false;
    bool                                   hasOrm = false;
    bool                                   hasAlpha = false;
    bool                                   hasZeroAlpha = false;
    float                                  semiAlphaFraction = 0.0f; // alpha>0のtexelのうち半透明の割合
};

} // namespace rs::render
