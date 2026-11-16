//
// CommandQueue.h - レンダリング層のコマンドキューの入れ物(rs::rhi)
//
// Device.h と同じ形で、ID3D12CommandQueue の
// 生ポインタを包んで公開ヘッダ越しに受け渡す。所有はしない(寿命は作った側=GpuContext等が持つ)。
// Native() は DX12 の .cpp の中でだけ使う。
//
#pragma once

struct ID3D12CommandQueue;

namespace rs::rhi {

class CommandQueue {
public:
    CommandQueue() = default;
    explicit CommandQueue(ID3D12CommandQueue* queue) : queue_(queue) {}

    ID3D12CommandQueue* Native() const { return queue_; }
    bool Valid() const { return queue_ != nullptr; }

private:
    ID3D12CommandQueue* queue_ = nullptr;
};

} // namespace rs::rhi
