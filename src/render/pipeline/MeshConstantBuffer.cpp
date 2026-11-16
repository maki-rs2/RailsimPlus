#include "MeshConstantBuffer.h"

#include <algorithm>
#include <utility>

#include <spdlog/spdlog.h>

namespace rs::render {

bool MeshConstantBuffer::Init(ID3D12Device* device) {
    cbStride_ = sizeof(MeshConstants);
    for (UINT f = 0; f < kFrames; ++f) {
        if (!CreateBuffer(device, f, 1)) return false;
    }
    return true;
}

bool MeshConstantBuffer::CreateBuffer(ID3D12Device* device, UINT f, UINT capacity) {
    if (cbMapped_[f]) {
        cbUpload_[f]->Unmap(0, nullptr);
        cbMapped_[f] = nullptr;
    }
    RetireBuffer(retirementQueue_, cbUpload_[f]);

    if (!CreatePooledUploadBuffer(device, UINT64(cbStride_) * capacity, cbUpload_[f])) {
        spdlog::warn("[mesh_pipeline] CreateConstantBuffer: 定数バッファ確保に失敗しました");
        cbCapacity_[f] = 0;
        return false;
    }
    D3D12_RANGE noRead = {0, 0};
    if (FAILED(cbUpload_[f]->Map(0, &noRead, reinterpret_cast<void**>(&cbMapped_[f])))) {
        spdlog::warn("[mesh_pipeline] CreateConstantBuffer: 定数バッファのMapに失敗しました");
        cbUpload_[f].Reset();
        cbMapped_[f] = nullptr;
        cbCapacity_[f] = 0;
        return false;
    }

    cbCapacity_[f] = capacity;
    return true;
}

bool MeshConstantBuffer::EnsureCapacity(ID3D12Device* device, UINT requiredSlots,
                                        UINT frameIndex) {
    const UINT f = frameIndex % kFrames;
    if (requiredSlots <= cbCapacity_[f]) return true;
    // 拡張は倍々で行い、大量submeshのメッシュでも再作成回数を抑える。
    const UINT grown = cbCapacity_[f] == 0 ? requiredSlots : cbCapacity_[f] * 2;
    const UINT newCapacity = std::max(requiredSlots, grown);
    if (!CreateBuffer(device, f, newCapacity)) {
        spdlog::warn("[mesh] cbUpload buffer growth failed (requested {} slots)",
                     newCapacity);
        return false;
    }
    return true;
}

D3D12_GPU_VIRTUAL_ADDRESS MeshConstantBuffer::GpuAddress(UINT f, UINT slot) const {
    return cbUpload_[f]->GetGPUVirtualAddress() + UINT64(slot) * cbStride_;
}

} // namespace rs::render
