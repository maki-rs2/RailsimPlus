//
// PrepassDepthTarget.h - 主視点の深度プリパスが書く深度専用ターゲット(D32_FLOAT、Reversed-Z)
//
// 本編の深度バッファ(MeshPipeline が持つ)とは別の資源で、本編の描画には影響しない。
// 資源は R32_TYPELESS で作り、DSV は D32_FLOAT、SRV は R32_FLOAT(AO のパスが読む)。
// DSV は自前の1枠ヒープ、SRV は呼び出し側が渡すシェーダ可視ヒープの1枠を使う。
// 大きさは描画先と同じで、リサイズ時は Resize で作り直す。状態(書き込み/読み取り)は内部で追跡する。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/CpuDescriptor.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"

namespace rs::render {

class PrepassDepthTarget {
public:
    PrepassDepthTarget();
    ~PrepassDepthTarget();
    PrepassDepthTarget(const PrepassDepthTarget&) = delete;
    PrepassDepthTarget& operator=(const PrepassDepthTarget&) = delete;

    // device・srvHeap は使い終わるまで生きていること(所有しない)。srvIndex は srvHeap の何番目の枠を
    // このターゲットのSRVに使うか。資源は Resize で作る。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t srvIndex);

    // 資源・DSV を(再)生成する。呼ぶ前に GPU がこの資源を使い終えていること。
    bool Resize(std::uint32_t width, std::uint32_t height);

    bool Ready() const;
    std::uint32_t Width() const;
    std::uint32_t Height() const;

    // 深度を書く状態へ遷移し、深度を遠方(Reversed-Z の 0)でクリアして、色の出力先なし・この深度だけを出力先にする。
    void Begin(const rs::rhi::CommandList& commandList);

    // 速度パス用: 色の出力先に rtv を、深度にこのターゲットの深度を(書き込み状態のまま、読み取りだけで)張る。
    // Begin で深度を書いたあと、EndWrite の前に呼ぶ。
    void BindWithColor(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor rtv);

    // ピクセルシェーダが読む状態へ遷移する(書き終えたあとに呼ぶ。すでにその状態なら何もしない)。
    void EndWrite(const rs::rhi::CommandList& commandList);

    // AO のパスが読むための SRV(R32_FLOAT。Reversed-Z の深度)。
    std::uint32_t SrvIndex() const;
    std::uint64_t SrvGpuHandle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
