//
// Device.h - レンダリング層のデバイスの入れ物(rs::rhi)
//
// 公開ヘッダのシグネチャにDX12型を直接出さないため、
// ID3D12Device の生ポインタをこの具象クラスで包んで受け渡す。所有はしない(デバイスの寿命は
// 作った側=GpuContext等が持つ)。Native() はDX12型を直接使う呼び出し側のための逃げ道で、DX12の
// .cpp の中でだけ使う。
//
#pragma once

struct ID3D12Device;

namespace rs::rhi {

class Device {
public:
    explicit Device(ID3D12Device* device) : device_(device) {}

    ID3D12Device* Native() const { return device_; }
    // デバイスを持っているか(nullptrで作ったものはfalse)。
    bool Valid() const { return device_ != nullptr; }

private:
    ID3D12Device* device_ = nullptr;
};

} // namespace rs::rhi
