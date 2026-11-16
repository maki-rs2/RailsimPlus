//
// MeshTransparency.h - MeshPipeline の半透明三角形・半透明描画用バッファ群
//
// 半透明三角形のリスト、動的インデックスバッファ、per-triangle マテリアル番号、
// 全マテリアル定数をまとめて管理する。
//
#pragma once

#include "MeshPipelineTypes.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace rs::render {

class MeshTransparency {
public:
    static constexpr UINT kFrames = 2;

    // srvHeap は shader-visible。firstTransBufDescriptorIndex 以降の gTriMat/gMats
    // descriptor をフレームごとに 2 枚ずつ使用する。
    void Init(ID3D12DescriptorHeap* srvHeap, UINT srvSize, UINT firstTransBufDescriptorIndex);

    std::vector<TransparentTri>& MutableTris() { return transparentTris_; }
    const std::vector<TransparentTri>& Tris() const { return transparentTris_; }
    bool Empty() const { return transparentTris_.empty(); }

    std::vector<MatData>& MutableMatDataCpu() { return matDataCpu_; }
    const std::vector<MatData>& MatDataCpu() const { return matDataCpu_; }

    void MarkAllContentDirty();
    bool ContentDirty(UINT f) const { return transContentDirty_[f]; }
    void ClearContentDirty(UINT f) { transContentDirty_[f] = false; }

    // 呼び出し前に半透明三角形と全マテリアル定数の CPU 配列を確定させる。
    // 枠を残している間は、その枠を飛ばす(ApplyPreservedFrameで作る)。
    bool UploadGpuBuffers();

    // frameの枠のGPUバッファと記述子を、次の ApplyPreservedFrame まで Reset / UploadGpuBuffers で
    // 変更しない。前フレームが実行中のままメッシュを差し替えるときに使う。もう一方の枠は対象から外す。
    void PreserveFrameForSwap(UINT frame);
    // frameの枠を残していたら、古いバッファを捨てて今の内容で作り直す。
    // frameのGPU処理が終わっている時点で呼ぶ。
    void ApplyPreservedFrame(UINT frame);
    // frameの枠のバッファが使える状態か(作り直しに失敗すると使えない)。
    bool FrameReady(UINT frame) const { return transIbMapped_[frame] != nullptr && triMatMapped_[frame] != nullptr; }

    const D3D12_INDEX_BUFFER_VIEW& IbView(UINT f) const { return transIbView_[f]; }
    uint8_t* IndexMapped(UINT f) const { return transIbMapped_[f]; }
    uint32_t* TriMatMapped(UINT f) const { return triMatMapped_[f]; }
    UINT FirstTransBufIndex() const { return firstTransBufIndex_; }
    std::vector<std::pair<float, const TransparentTri*>>& SortScratch() { return transSort_; }

    void SyncMatDataToGpu(UINT f);
    void Reset();

private:
    bool BuildFrameBuffers(UINT frame);
    void ReleaseFrameBuffers(UINT frame);

    bool                  preservedFrame_[kFrames] = {false, false};
    ID3D12DescriptorHeap* srvHeap_ = nullptr;
    UINT                   srvSize_ = 0;
    UINT                   firstTransBufIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> transIb_[kFrames];
    uint8_t*                                transIbMapped_[kFrames] = {};
    D3D12_INDEX_BUFFER_VIEW                 transIbView_[kFrames] = {};

    Microsoft::WRL::ComPtr<ID3D12Resource> triMatBuf_[kFrames];
    uint32_t*                               triMatMapped_[kFrames] = {};

    Microsoft::WRL::ComPtr<ID3D12Resource> matDataBuf_[kFrames];
    MatData*                                matDataMapped_[kFrames] = {};
    std::vector<MatData>                    matDataCpu_;

    std::vector<TransparentTri> transparentTris_;
    std::vector<std::pair<float, const TransparentTri*>> transSort_;
    bool transContentDirty_[kFrames] = {true, true};
};

} // namespace rs::render
