//
// MeshConstantBuffer.h - MeshPipelineのsubmesh定数バッファ(データクラスタ)
//
// 「フレームごとのsubmesh定数バッファ」責務
// (cbUpload_/cbMapped_/cbStride_/cbCapacity_)だけを持つクラス。
// UploadMesh()がEnsureCapacity()で容量を確定し、
// Record*系が毎フレームWriteMeshConstants()経由で書き込む。
//
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include "MeshPipelineTypes.h"

namespace rs::render {

class MeshConstantBuffer {
public:
    static constexpr UINT kFrames = 2;

    // cbStride_=sizeof(MeshConstants)を設定し、両フレーム分を最小容量1枠で確保する
    // (実際に必要な容量はUploadMesh()側がEnsureCapacity()で確定する)。失敗時false。
    bool Init(ID3D12Device* device);

    // requiredSlots(=submeshes.size()+2)を満たすよう、フレーム frameIndex の定数
    // バッファを必要なら doubling で育てる。MeshDynamicBuffers::EnsurePartWorldCapacity
    // と同じ形。UploadMesh から kFrames 両方に対して呼ぶこと(部分更新で追従を待てる
    // gPartWorld と異なり、submesh数はメッシュ差し替えのたびに不連続に変わるため)。
    bool EnsureCapacity(ID3D12Device* device, UINT requiredSlots, UINT frameIndex);

    void SetRetirementQueue(rs::rhi::RetirementQueue* queue) { retirementQueue_ = queue; }

    UINT Capacity(UINT f) const { return cbCapacity_[f]; }
    uint8_t* Mapped(UINT f) const { return cbMapped_[f]; }
    UINT Stride() const { return cbStride_; }
    D3D12_GPU_VIRTUAL_ADDRESS GpuAddress(UINT f, UINT slot) const;

private:
    bool CreateBuffer(ID3D12Device* device, UINT f, UINT capacity);

    Microsoft::WRL::ComPtr<ID3D12Resource> cbUpload_[kFrames];
    uint8_t*                               cbMapped_[kFrames] = {nullptr, nullptr};
    UINT                                   cbStride_ = 0;
    UINT                                   cbCapacity_[kFrames] = {};
    rs::rhi::RetirementQueue* retirementQueue_ = nullptr;
};

} // namespace rs::render
