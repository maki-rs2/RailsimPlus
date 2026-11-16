#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "GlyphInfo.h"
#include "OverlayGeometryBuilder.h"

namespace rs::render {

class GlyphAtlas : public OverlayGeometryBuilder::GlyphProvider {
public:
    ~GlyphAtlas();
    bool Init(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvStride,
              UINT srvIndex, uint32_t pixelSize, uint32_t initialSize, const char* fontPath,
              long fontFaceIndex);
    bool GetGlyph(uint32_t codePoint, GlyphInfo& glyph) override;
    bool UploadDirty(ID3D12GraphicsCommandList* cmdList, UINT frameIndex);
    bool Ready() const { return ready_; }
    D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle() const { return gpuHandle_; }

private:
    bool AddGlyph(uint32_t codePoint, GlyphInfo& glyph);
    bool AllocateRect(uint32_t width, uint32_t height, uint32_t& x, uint32_t& y);
    void WarnOnce(const char* message) const;

    Microsoft::WRL::ComPtr<ID3D12Resource> texture_;
    static constexpr UINT kUploadRingSize = 2;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kUploadRingSize> uploadBuffers_;
    std::unordered_map<uint32_t, GlyphInfo> glyphs_;
    std::vector<uint8_t> pixels_;
    void* library_ = nullptr;
    void* face_ = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle_ = {};
    uint32_t size_ = 0;
    uint32_t pixelSize_ = 0;
    uint32_t shelfX_ = 0;
    uint32_t shelfY_ = 0;
    uint32_t shelfHeight_ = 0;
    bool ready_ = false;
    bool dirty_ = false;
    mutable bool warned_ = false;
};
} // namespace rs::render
