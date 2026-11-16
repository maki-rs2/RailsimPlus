//
// LandscapeCubeImpl.h - LandscapeCube の内部実装(DX12型を持つ。LandscapeCube*.cpp だけが include する)
//
// LandscapeCube.cpp: 画像の読込・資源の作成・公開API。LandscapeCubeBake.cpp: PSOと焼き込みの記録。
//
#pragma once

#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>

#include "../postprocess/PostProcessFormats.h"
#include "../resource/TextureLoader.h"
#include "IblParams.h"
#include "LandscapeCube.h"

namespace rs::render {

namespace landscape_cube {

inline constexpr std::uint32_t kFaceCount = ibl::kIblCubeFaceCount;

constexpr std::uint32_t MipCountFor(std::uint32_t size) {
    std::uint32_t count = 1;
    while ((size >> count) > 0) ++count;
    return count;
}
inline constexpr std::uint32_t kMipCount = MipCountFor(LandscapeCube::kFaceSize);
static_assert(kMipCount == 11, "1024,512,...,1");

// 自前のSRVヒープの並び: 0=元画像、m(1以上)=ミップ m-1 の6面(Texture2DArray、ミップ m の生成の入力)。
inline constexpr std::uint32_t kPrivateSrvCount = kMipCount;

// 面のカメラ(右・上・前)をHLSLの FaceConstants(float3+pad が3組、12 DWORD)へ詰める。
struct FaceShaderConstants {
    float right[3]; float padding0;
    float up[3]; float padding1;
    float forward[3]; float padding2;
};
struct DownsampleShaderConstants {
    std::uint32_t face;
};
inline constexpr UINT kFaceConstantDwords = sizeof(FaceShaderConstants) / sizeof(std::uint32_t);
inline constexpr UINT kDownsampleConstantDwords = sizeof(DownsampleShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(FaceShaderConstants) == 48 && offsetof(FaceShaderConstants, up) == 16 &&
              offsetof(FaceShaderConstants, forward) == 32, "FaceConstants と配置を一致させる");
static_assert(sizeof(DownsampleShaderConstants) == 4 && offsetof(DownsampleShaderConstants, face) == 0,
              "DownsampleConstants の uint と配置を一致させる");

inline D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource, UINT subresource,
                                         D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = subresource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    return barrier;
}

// RTVヒープ内の位置(CubeTarget と同じ: ミップの外側ループ、面の内側ループ)。
inline UINT RtvIndex(std::uint32_t face, std::uint32_t mip) { return mip * kFaceCount + face; }
// 資源のサブリソース番号(配列スライスの外側ループ、ミップの内側ループ)。
inline UINT SubresourceIndex(std::uint32_t face, std::uint32_t mip) { return mip + face * kMipCount; }

} // namespace landscape_cube

using Microsoft::WRL::ComPtr;

struct LandscapeCube::Impl {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12Resource> image;
    ComPtr<ID3D12Resource> cube;
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> privateSrvHeap;
    UINT rtvStride = 0;
    UINT privateSrvStride = 0;
    D3D12_VERTEX_BUFFER_VIEW vertexView = {};
    D3D12_INDEX_BUFFER_VIEW indexView = {};
    UINT indexCount = 0;
    ComPtr<ID3D12RootSignature> rootSigMesh;
    ComPtr<ID3D12RootSignature> rootSigDownsample;
    ComPtr<ID3D12PipelineState> psoMesh;
    ComPtr<ID3D12PipelineState> psoDownsample;
    std::uint64_t sharedSrvGpuHandle = 0;
    bool baked = false;
    LandscapeContrastStats contrastStats;

    bool UploadImage(const rs::render::ImageData& pixels, ID3D12CommandQueue* queue);
    bool CreateCube(const rs::rhi::SrvHeap& sharedHeap, std::uint32_t srvIndex);
    bool CreateMesh();
    bool CreatePipelines();

    D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle(std::uint32_t face, std::uint32_t mip) const {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(landscape_cube::RtvIndex(face, mip)) * rtvStride;
        return rtv;
    }
    D3D12_GPU_DESCRIPTOR_HANDLE PrivateSrvGpu(std::uint32_t index) const {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = privateSrvHeap->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(index) * privateSrvStride;
        return handle;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE PrivateSrvCpu(std::uint32_t index) const {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = privateSrvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * privateSrvStride;
        return handle;
    }

    void BindFace(ID3D12GraphicsCommandList* list, std::uint32_t face, std::uint32_t mip) const {
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = RtvHandle(face, mip);
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const std::uint32_t size = std::max<std::uint32_t>(1, kFaceSize >> mip);
        const D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f};
        const D3D12_RECT scissor = {0, 0, static_cast<LONG>(size), static_cast<LONG>(size)};
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissor);
    }

    void RecordBake(ID3D12GraphicsCommandList* list) const;
};

} // namespace rs::render
