//
// VolumeTexture.h - compute で書いてシェーダで読む3Dテクスチャ(RGBA16F)と、その SRV・UAV
//
// SRV と UAV は共有 SRV ヒープの連続2枠を使う。状態(読む/書く)は内部で追跡し、Transition は必要な遷移だけを記録する。
// 生成直後は読む状態。Resize は古い資源をその場で解放するので、GPU が使っていない間に呼ぶこと。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"

namespace rs::render {

enum class VolumeTextureState {
    ShaderResource,  // ピクセル・compute のシェーダが読む状態
    UnorderedAccess, // compute が書く状態
};

class VolumeTexture {
public:
    static constexpr std::uint32_t kSrvCount = 2; // SRV・UAV の順

    VolumeTexture();
    ~VolumeTexture();
    VolumeTexture(const VolumeTexture&) = delete;
    VolumeTexture& operator=(const VolumeTexture&) = delete;

    // 枠の情報を覚える(資源は Resize で作る)。device・srvHeap は使い終わるまで生きていること。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex);
    bool Resize(std::uint32_t width, std::uint32_t height, std::uint32_t depth);

    bool Ready() const;
    std::uint32_t Width() const;
    std::uint32_t Height() const;
    std::uint32_t Depth() const;
    // シェーダが ResourceDescriptorHeap[] で引く絶対番号。
    std::uint32_t SrvIndex() const;
    std::uint32_t UavIndex() const;

    void Transition(const rs::rhi::CommandList& commandList, VolumeTextureState state);
    // 同じ UAV への書き込みを続けて行うときの、書き込みの完了待ちを記録する。
    void UavBarrier(const rs::rhi::CommandList& commandList) const;

    // 全スライスの読み戻しに要るバイト数と行のバイト数(行はコピーの揃えに合わせる)。
    std::uint64_t ReadbackByteSize() const;
    std::uint32_t ReadbackRowPitch() const;
    // destBuffer(ネイティブのバッファ資源。コピー先の状態)へ全スライスを写す。読む状態へ戻してから返る。
    bool RecordCopyToBuffer(const rs::rhi::CommandList& commandList, void* destBuffer);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
