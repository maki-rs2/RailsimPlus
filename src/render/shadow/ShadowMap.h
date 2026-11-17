//
// ShadowMap.h - 太陽光のシャドウマップ(D32_FLOAT の Texture2DArray)とそのDSV・SRV
//
// スライス1枚がカスケード1枚。配列にしておくのは、カスケードを複数枚に増やしても資源の形が変わらないため。
// 深度は通常方向(近=0、遠=1)で、主深度バッファの Reversed-Z とは独立。
// 資源は R32_TYPELESS で作り、DSV は D32_FLOAT、SRV は R32_FLOAT(比較サンプラで引く)。
// 状態(シェーダが読む/深度を書く)は内部で追跡し、Transition は必要な遷移だけを記録する。
// 生成直後と描いた後はシェーダが読む状態。SRVは共有SRVヒープの1枠。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/CpuDescriptor.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"

namespace rs::render {

enum class ShadowMapState {
    ShaderResource, // 受ける側のシェーダが読む状態(生成直後・描画後)
    DepthWrite,     // シャドウパスが書く状態
};

class ShadowMap {
public:
    ShadowMap();
    ~ShadowMap();
    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    // 資源・DSV・SRVを作る。size は一辺[テクセル]、sliceCount は配列の枚数。srvIndex は srvHeap の何番目の枠を
    // SRVに使うか。device・srvHeap は使い終わるまで生きていること(所有しない)。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t srvIndex,
              std::uint32_t size, std::uint32_t sliceCount);

    bool Ready() const;
    std::uint32_t Size() const;
    std::uint32_t SliceCount() const;
    // シェーダが bindless テーブルの配列で引くSRVの絶対番号。
    std::uint32_t SrvIndex() const;

    // 必要なら状態遷移のバリアを記録する(同じ状態なら何もしない)。
    void Transition(const rs::rhi::CommandList& commandList, ShadowMapState state);

    // slice の深度を1.0で消し、深度だけを書く出力先(色なし)にして、ビューポートとシザーを全面にする。
    // DepthWrite 状態にしてから呼ぶこと。
    void BeginSlice(const rs::rhi::CommandList& commandList, std::uint32_t slice) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
