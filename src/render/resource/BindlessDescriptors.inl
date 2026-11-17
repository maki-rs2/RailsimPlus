// DX12 の .cpp からだけ使う内部実装。公開ヘッダには含めない。
#pragma once

#include <d3d12.h>

#include <array>
#include <climits>

namespace rs::render {

using BindlessDescriptorRanges = std::array<D3D12_DESCRIPTOR_RANGE, 7>;

inline BindlessDescriptorRanges MakeBindlessDescriptorRanges() {
    return {{
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 100, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 101, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 102, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 103, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 104, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 105, 0},
        {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UINT_MAX, 0, 106, 0},
    }};
}

// ranges はルート署名のシリアライズが終わるまで保持する。
inline D3D12_ROOT_PARAMETER MakeBindlessDescriptorTable(const BindlessDescriptorRanges& ranges) {
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(ranges.size());
    param.DescriptorTable.pDescriptorRanges = ranges.data();
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    return param;
}

inline void SetGraphicsBindlessTable(ID3D12GraphicsCommandList* list, UINT rootParam, ID3D12DescriptorHeap* heap) {
    list->SetGraphicsRootDescriptorTable(rootParam, heap->GetGPUDescriptorHandleForHeapStart());
}

inline void SetComputeBindlessTable(ID3D12GraphicsCommandList* list, UINT rootParam, ID3D12DescriptorHeap* heap) {
    list->SetComputeRootDescriptorTable(rootParam, heap->GetGPUDescriptorHandleForHeapStart());
}

} // namespace rs::render
