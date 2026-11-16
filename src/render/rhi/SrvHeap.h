//
// SrvHeap.h - シェーダから見える SRV ヒープ(とその1枠の大きさ)の入れ物(rs::rhi)
//
// Device.h と同じ形で、ID3D12DescriptorHeap の
// 生ポインタを包んで公開ヘッダ越しに受け渡す。所有はしない。Native() は DX12 の .cpp の
// 中でだけ使う。
//
#pragma once

struct ID3D12DescriptorHeap;

namespace rs::rhi {

class SrvHeap {
public:
    SrvHeap() = default;
    SrvHeap(ID3D12DescriptorHeap* heap, unsigned int stride) : heap_(heap), stride_(stride) {}

    ID3D12DescriptorHeap* Native() const { return heap_; }
    // ディスクリプタ1枠のバイト数(GetDescriptorHandleIncrementSize の値)。
    unsigned int Stride() const { return stride_; }
    bool Valid() const { return heap_ != nullptr && stride_ != 0; }

private:
    ID3D12DescriptorHeap* heap_ = nullptr;
    unsigned int stride_ = 0;
};

} // namespace rs::rhi
