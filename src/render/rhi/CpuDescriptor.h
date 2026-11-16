//
// CpuDescriptor.h - CPU側ディスクリプタハンドル(RTV/DSV)の入れ物(rs::rhi)
//
// D3D12_CPU_DESCRIPTOR_HANDLE と同じ中身(ptr 1つ)で、
// 公開ヘッダのシグネチャに D3D12_* を出さずにハンドルを受け渡すために使う。
// DX12側の .cpp では D3D12_CPU_DESCRIPTOR_HANDLE{ptr} と相互に詰め替える。
//
#pragma once

#include <cstddef>

namespace rs::rhi {

struct CpuDescriptor {
    std::size_t ptr = 0;
    bool Valid() const { return ptr != 0; }
};

} // namespace rs::rhi
