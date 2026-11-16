#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <ResourceUploadBatch.h>

#include <cstdint>

#include "TextureLoader.h"

namespace rs::render {

// img.ok=false の場合は fallbackRGBA(0xAABBGGRR順)の1x1プレースホルダーを作る。
// DEFAULT ヒープへ作成し、ステージング(UPLOADヒープ確保・行単位コピー・
// CopyTextureRegion・リソースバリア)は DirectXTK12 の ResourceUploadBatch に
// 委譲する(upload はステージングリソースの寿命も内部で管理するため、
// 呼び出し側の keepAlive は不要)。呼び出し側は upload.Begin()/End() で
// 1回のバッチにまとめること(1テクスチャごとに Begin/End すると
// 大量のキュー submission になり読込が遅くなる)。SRV はここでは作らない
// (配置先の descriptor slot は呼び出し側の責務。WriteTextureSrv を別途呼ぶこと)。
// mipIgnoresAlpha=true なら、画像のアルファに関係なくフルミップチェーンを作る
// (法線マップ・ORMのようにアルファを抜きに使わないテクスチャ用。hasAlphaOut等は画像の実際のアルファを返す)。
Microsoft::WRL::ComPtr<ID3D12Resource> CreateGpuTexture(
    ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
    const ImageData& imgIn, uint32_t fallbackRGBA, bool isSrgb, bool* hasAlphaOut = nullptr,
    bool* hasZeroAlphaOut = nullptr, bool mipIgnoresAlpha = false, float* semiAlphaFractionOut = nullptr);

// 作成済みテクスチャリソースへの SRV を srvHeap 内の slot 位置へ書き込む。
void WriteTextureSrv(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvSize,
                      ID3D12Resource* texture, UINT slot, bool isSrgb);

// 作成済み構造化バッファへの SRV を srvHeap 内の slot 位置へ書き込む。
void WriteStructuredBufferSrv(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvSize,
                              ID3D12Resource* buffer, UINT numElements,
                              UINT structureByteStride, UINT slot);

} // namespace rs::render
