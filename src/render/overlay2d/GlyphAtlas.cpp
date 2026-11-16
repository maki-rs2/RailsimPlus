#include "GlyphAtlas.h"

#include <algorithm>
#include <cstring>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <spdlog/spdlog.h>

namespace rs::render {
namespace {

constexpr uint32_t kAtlasPadding = 1;

} // namespace

GlyphAtlas::~GlyphAtlas() {
    if (face_ != nullptr) FT_Done_Face(static_cast<FT_Face>(face_));
    if (library_ != nullptr) FT_Done_FreeType(static_cast<FT_Library>(library_));
}

bool GlyphAtlas::Init(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvStride,
                      UINT srvIndex, uint32_t pixelSize, uint32_t initialSize, const char* fontPath,
                      long fontFaceIndex) {
    if (device == nullptr || srvHeap == nullptr || srvStride == 0 || pixelSize == 0 || initialSize == 0 ||
        fontPath == nullptr || fontPath[0] == '\0') {
        WarnOnce("グリフアトラスの初期化引数が不正です");
        return false;
    }
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) {
        WarnOnce("FreeTypeの初期化に失敗しました");
        return false;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(library, fontPath, fontFaceIndex, &face) != 0 ||
        FT_Set_Pixel_Sizes(face, 0, pixelSize) != 0) {
        if (face != nullptr) FT_Done_Face(face);
        FT_Done_FreeType(library);
        WarnOnce("フォントの読み込みに失敗しました");
        return false;
    }
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = initialSize;
    textureDesc.Height = initialSize;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                                IID_PPV_ARGS(&texture_)))) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        WarnOnce("グリフアトラスのテクスチャ作成に失敗しました");
        return false;
    }
    const uint32_t uploadRowPitch = (initialSize + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                                    ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    const UINT64 uploadSize = static_cast<UINT64>(uploadRowPitch) * initialSize;
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                IID_PPV_ARGS(&uploadBuffers_[0])))) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        WarnOnce("グリフアトラスのアップロードバッファ作成に失敗しました");
        return false;
    }
    for (UINT index = 1; index < kUploadRingSize; ++index) {
        if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    IID_PPV_ARGS(&uploadBuffers_[index])))) {
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            WarnOnce("グリフアトラスのアップロードバッファ作成に失敗しました");
            return false;
        }
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += static_cast<SIZE_T>(srvIndex) * srvStride;
    device->CreateShaderResourceView(texture_.Get(), &srv, cpuHandle);
    gpuHandle_ = srvHeap->GetGPUDescriptorHandleForHeapStart();
    gpuHandle_.ptr += static_cast<UINT64>(srvIndex) * srvStride;
    library_ = library;
    face_ = face;
    size_ = initialSize;
    pixelSize_ = pixelSize;
    pixels_.resize(static_cast<size_t>(initialSize) * initialSize);
    ready_ = true;
    return true;
}

bool GlyphAtlas::AllocateRect(uint32_t width, uint32_t height, uint32_t& x, uint32_t& y) {
    if (width > size_ || height > size_) return false;
    if (shelfX_ + width > size_) {
        shelfX_ = 0;
        shelfY_ += shelfHeight_;
        shelfHeight_ = 0;
    }
    if (shelfY_ + height > size_) return false;
    x = shelfX_;
    y = shelfY_;
    shelfX_ += width;
    shelfHeight_ = std::max(shelfHeight_, height);
    return true;
}

bool GlyphAtlas::AddGlyph(uint32_t codePoint, GlyphInfo& glyph) {
    FT_Face face = static_cast<FT_Face>(face_);
    if (FT_Load_Char(face, codePoint, FT_LOAD_RENDER) != 0) return false;
    const FT_GlyphSlot slot = face->glyph;
    glyph.advancePx = static_cast<float>(slot->advance.x) / 64.0f;
    glyph.bearingXPx = static_cast<float>(slot->bitmap_left);
    glyph.bearingYPx = static_cast<float>(slot->bitmap_top);
    glyph.widthPx = static_cast<float>(slot->bitmap.width);
    glyph.heightPx = static_cast<float>(slot->bitmap.rows);
    if (slot->bitmap.width == 0 || slot->bitmap.rows == 0) return true;
    uint32_t x = 0;
    uint32_t y = 0;
    if (!AllocateRect(slot->bitmap.width + kAtlasPadding * 2, slot->bitmap.rows + kAtlasPadding * 2, x, y)) {
        WarnOnce("グリフアトラスの容量が不足しました");
        return false;
    }
    for (uint32_t row = 0; row < slot->bitmap.rows; ++row) {
        const uint8_t* source = slot->bitmap.buffer + static_cast<ptrdiff_t>(row) * slot->bitmap.pitch;
        uint8_t* destination = pixels_.data() + static_cast<size_t>(y + kAtlasPadding + row) * size_ + x + kAtlasPadding;
        std::memcpy(destination, source, slot->bitmap.width);
    }
    const float inverseSize = 1.0f / static_cast<float>(size_);
    glyph.u0 = static_cast<float>(x + kAtlasPadding) * inverseSize;
    glyph.v0 = static_cast<float>(y + kAtlasPadding) * inverseSize;
    glyph.u1 = glyph.u0 + glyph.widthPx * inverseSize;
    glyph.v1 = glyph.v0 + glyph.heightPx * inverseSize;
    dirty_ = true;
    return true;
}

bool GlyphAtlas::GetGlyph(uint32_t codePoint, GlyphInfo& glyph) {
    if (!ready_) return false;
    const std::unordered_map<uint32_t, GlyphInfo>::const_iterator it = glyphs_.find(codePoint);
    if (it != glyphs_.end()) {
        glyph = it->second;
        return true;
    }
    GlyphInfo created;
    if (!AddGlyph(codePoint, created)) return false;
    glyphs_.emplace(codePoint, created);
    glyph = created;
    return true;
}

bool GlyphAtlas::UploadDirty(ID3D12GraphicsCommandList* cmdList, UINT frameIndex) {
    if (!ready_ || !dirty_ || cmdList == nullptr) return ready_;
    ID3D12Resource* const uploadBuffer = uploadBuffers_[frameIndex % kUploadRingSize].Get();
    void* mapped = nullptr;
    const D3D12_RANGE readRange = {0, 0};
    if (FAILED(uploadBuffer->Map(0, &readRange, &mapped))) {
        spdlog::error("[glyph_atlas] アップロードバッファのMapに失敗しました");
        return false;
    }
    const uint32_t rowPitch = (size_ + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                              ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    uint8_t* uploadBytes = static_cast<uint8_t*>(mapped);
    for (uint32_t row = 0; row < size_; ++row) {
        std::memcpy(uploadBytes + static_cast<size_t>(row) * rowPitch,
                    pixels_.data() + static_cast<size_t>(row) * size_, size_);
    }
    uploadBuffer->Unmap(0, nullptr);
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture_.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = uploadBuffer;
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_UNORM;
    source.PlacedFootprint.Footprint.Width = size_;
    source.PlacedFootprint.Footprint.Height = size_;
    source.PlacedFootprint.Footprint.Depth = 1;
    source.PlacedFootprint.Footprint.RowPitch = rowPitch;
    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = texture_.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    cmdList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    cmdList->ResourceBarrier(1, &barrier);
    dirty_ = false;
    return true;
}

void GlyphAtlas::WarnOnce(const char* message) const {
    if (warned_) return;
    warned_ = true;
    spdlog::warn("[overlay2d] {}。テキスト描画をスキップします", message);
}

} // namespace rs::render
