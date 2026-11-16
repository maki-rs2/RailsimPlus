// MeshTransparency.cpp - MeshPipeline の半透明三角形・半透明描画用バッファ群
#include "MeshTransparency.h"

#include "../resource/TextureUpload.h"

#include <cstring>

#include <spdlog/spdlog.h>

namespace rs::render {

void MeshTransparency::Init(ID3D12DescriptorHeap* srvHeap, UINT srvSize,
                            UINT firstTransBufDescriptorIndex) {
    srvHeap_ = srvHeap;
    srvSize_ = srvSize;
    firstTransBufIndex_ = firstTransBufDescriptorIndex;
}

void MeshTransparency::MarkAllContentDirty() {
    for (bool& dirty : transContentDirty_) dirty = true;
}

bool MeshTransparency::UploadGpuBuffers() {
    for (UINT f = 0; f < kFrames; ++f) {
        if (preservedFrame_[f]) continue;
        if (!BuildFrameBuffers(f)) return false;
    }
    return true;
}

bool MeshTransparency::BuildFrameBuffers(UINT f) {
    Microsoft::WRL::ComPtr<ID3D12Device> deviceRef;
    if (FAILED(srvHeap_->GetDevice(IID_PPV_ARGS(&deviceRef)))) return false;
    ID3D12Device* device = deviceRef.Get();
    auto makeUploadBuf = [device](UINT64 bytes) -> Microsoft::WRL::ComPtr<ID3D12Resource> {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = bytes;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Microsoft::WRL::ComPtr<ID3D12Resource> res;
        if (FAILED(device->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&res))))
            return nullptr;
        return res;
    };

    const UINT64 matBytes = UINT64(matDataCpu_.size()) * sizeof(MatData);
    matDataBuf_[f] = makeUploadBuf(matBytes);
    if (!matDataBuf_[f]) return false;
    D3D12_RANGE noRead = {0, 0};
    if (FAILED(matDataBuf_[f]->Map(0, &noRead, reinterpret_cast<void**>(&matDataMapped_[f])))) {
        spdlog::error("[mesh] 半透明材質バッファのMapに失敗しました frame={}", f);
        ReleaseFrameBuffers(f);
        return false;
    }
    std::memcpy(matDataMapped_[f], matDataCpu_.data(), size_t(matBytes));

    const UINT64 triCount = transparentTris_.size();
    const UINT64 transIbBytes = triCount * 3 * sizeof(uint32_t);
    const UINT64 triMatBytes = triCount * sizeof(uint32_t);
    transIb_[f] = makeUploadBuf(transIbBytes);
    if (!transIb_[f]) return false;
    if (FAILED(transIb_[f]->Map(0, &noRead, reinterpret_cast<void**>(&transIbMapped_[f])))) {
        spdlog::error("[mesh] 半透明索引バッファのMapに失敗しました frame={}", f);
        ReleaseFrameBuffers(f);
        return false;
    }
    transIbView_[f].BufferLocation = transIb_[f]->GetGPUVirtualAddress();
    transIbView_[f].Format = DXGI_FORMAT_R32_UINT;
    transIbView_[f].SizeInBytes = UINT(transIbBytes);

    triMatBuf_[f] = makeUploadBuf(triMatBytes);
    if (!triMatBuf_[f]) return false;
    if (FAILED(triMatBuf_[f]->Map(0, &noRead, reinterpret_cast<void**>(&triMatMapped_[f])))) {
        spdlog::error("[mesh] 三角形材質バッファのMapに失敗しました frame={}", f);
        ReleaseFrameBuffers(f);
        return false;
    }

    const UINT tri = firstTransBufIndex_ + f * 2;
    WriteStructuredBufferSrv(device, srvHeap_, srvSize_, triMatBuf_[f].Get(), UINT(triCount),
                             sizeof(uint32_t), tri);
    WriteStructuredBufferSrv(device, srvHeap_, srvSize_, matDataBuf_[f].Get(),
                             UINT(matDataCpu_.size()), sizeof(MatData), tri + 1);
    return true;
}

void MeshTransparency::PreserveFrameForSwap(UINT frame) {
    for (UINT f = 0; f < kFrames; ++f) preservedFrame_[f] = f == frame;
}

void MeshTransparency::ApplyPreservedFrame(UINT frame) {
    if (!preservedFrame_[frame]) return;
    preservedFrame_[frame] = false;
    ReleaseFrameBuffers(frame);
    if (transparentTris_.empty()) return;
    if (!BuildFrameBuffers(frame)) {
        ReleaseFrameBuffers(frame); // 途中まで作ったものを残さない(FrameReadyがfalseになり描かれない)
        spdlog::warn("[mesh] 半透明バッファを作れません frame={}", frame);
        return;
    }
    transContentDirty_[frame] = true;
}

void MeshTransparency::SyncMatDataToGpu(UINT f) {
    if (matDataMapped_[f] && !matDataCpu_.empty()) {
        std::memcpy(matDataMapped_[f], matDataCpu_.data(), matDataCpu_.size() * sizeof(MatData));
    }
}

void MeshTransparency::ReleaseFrameBuffers(UINT f) {
    if (transIbMapped_[f]) {
        transIb_[f]->Unmap(0, nullptr);
        transIbMapped_[f] = nullptr;
    }
    transIb_[f].Reset();
    transIbView_[f] = {};
    if (triMatMapped_[f]) {
        triMatBuf_[f]->Unmap(0, nullptr);
        triMatMapped_[f] = nullptr;
    }
    triMatBuf_[f].Reset();
    // matDataBuf_ は常駐 Map しているため明示 Unmap してから破棄する。
    if (matDataMapped_[f]) {
        matDataBuf_[f]->Unmap(0, nullptr);
        matDataMapped_[f] = nullptr;
    }
    matDataBuf_[f].Reset();
}

void MeshTransparency::Reset() {
    transparentTris_.clear();
    for (UINT f = 0; f < kFrames; ++f) {
        if (!preservedFrame_[f]) ReleaseFrameBuffers(f);
    }
    matDataCpu_.clear();
}

} // namespace rs::render
