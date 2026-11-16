#pragma once

#include <array>

#include <d3d12.h>
#include <wrl/client.h>

#include "OverlayGeometryBuilder.h"
#include "GlyphAtlas.h"

namespace rs::render {

class Overlay2DPass {
public:
    struct Settings {
        float occludedAlphaScale = 0.25f;
        float aaSmoothingWidthPx = 1.0f;
        float dashDuty = 0.5f;
        float attenuationReferenceDistance = 100.0f;
        float attenuationMinScale = 0.35f;
        uint32_t textPixelSize = 14;
        uint32_t atlasInitialSize = 1024;
    };

    bool Init(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvStride,
              UINT glyphSrvIndex, DXGI_FORMAT rtvFormat, const char* fontPath, long fontFaceIndex);
    void Record(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv, const rs::core::DebugDrawList& list,
                const rs::core::Mat4& viewProj, UINT width, UINT height, UINT frameIndex);

private:
    static constexpr UINT kUploadRingSize = 2;

    bool CreateRootSignature(ID3D12Device* device);
    bool CreatePipelines(ID3D12Device* device, DXGI_FORMAT rtvFormat);
    bool EnsureUploadBuffer(UINT frameIndex, size_t vertexCount, size_t vertexSize,
                            std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kUploadRingSize>& buffers,
                            std::array<size_t, kUploadRingSize>& capacities);
    void RecordGeometry(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* pso,
                        float alphaScale, UINT width, UINT height, UINT uploadSlot,
                        UINT firstVertex, UINT vertexCount);
    void RecordText(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* pso,
                    float alphaScale, UINT width, UINT height, UINT uploadSlot,
                    UINT firstVertex, UINT vertexCount);

    Settings settings_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> visiblePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> occludedPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> alwaysOnTopPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> textVisiblePso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> textOccludedPso_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> textAlwaysOnTopPso_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kUploadRingSize> uploadBuffers_;
    std::array<size_t, kUploadRingSize> uploadCapacities_ = {};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kUploadRingSize> textUploadBuffers_;
    std::array<size_t, kUploadRingSize> textUploadCapacities_ = {};
    GlyphAtlas glyphAtlas_;
    // 頂点配列はフレームをまたいで使い回す(毎フレーム数MBを確保し直すのを避ける)。
    OverlayGeometry geometry_;
    ID3D12Device* device_ = nullptr;
    bool ready_ = false;
};

} // namespace rs::render
