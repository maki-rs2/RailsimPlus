//
// CommandList.h - レンダリング層のコマンドリストの入れ物(rs::rhi)
//
// Device.h と同じ形で、ID3D12GraphicsCommandList の
// 生ポインタを包んで公開ヘッダ越しに受け渡す。所有はしない(寿命は記録を始めた側が持つ)。
// Native() は DX12 の .cpp の中でだけ使う。
//
#pragma once

struct ID3D12GraphicsCommandList;

namespace rs::rhi {

class CommandList {
public:
    explicit CommandList(ID3D12GraphicsCommandList* commandList) : commandList_(commandList) {}

    ID3D12GraphicsCommandList* Native() const { return commandList_; }
    bool Valid() const { return commandList_ != nullptr; }

    // ビューポートとシザーを、同じ矩形(ピクセル)に設定する。
    void SetViewportAndScissor(float x, float y, float width, float height) const;

private:
    ID3D12GraphicsCommandList* commandList_ = nullptr;
};

} // namespace rs::rhi
