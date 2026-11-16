//
// TaaPass.h - TAA の解決パス(時間方向のアンチエイリアス)
//
// 今フレームの HDR(ジッタ付きで描いたもの)と履歴を、深度・速度から求めた動きで重ね、解決後の HDR を作る。
// 解決後の HDR は、続くレンズフレア・ブルーム・トーンマップの入力になり、同じ内容を履歴へもコピーする
// (レンズフレアは解決後に足すので、履歴には入らない)。
// 履歴・解決後の HDR は画面と同じ大きさの Half16 で、SRV 枠は呼び出し側が渡す連続枠の先頭から
// 履歴・解決後・(本編の深度。呼び出し側が書く)の順に使う。
// DX12 の型は .cpp に閉じる。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "SceneHdrTarget.h"

namespace rs::render {

// 1フレームぶんの入力。行列は row-vector 慣習の row-major(16 要素)。
struct TaaFrameParams {
    float invViewProj[16] = {};  // 今フレームの(ジッタなしの)viewProj の逆行列
    float prevViewProj[16] = {}; // 前フレームの(ジッタなしの)viewProj
    float jitterNdc[2] = {};     // 今フレームのずれ(NDC)
    float historyWeight = 0.9f;  // 履歴の重み(静止時)
    float sharpness = 0.0f;      // 解決後に足すシャープ(0 で無し)
    bool resetHistory = false;   // 履歴を使わない(今フレームだけを出す)
    std::uint32_t velocitySrvIndex = 0; // 速度バッファ(物体の動き)の SRV 番号
};

class TaaPass {
public:
    // SRV 枠の数(履歴・解決後・本編の深度)。firstSrvIndex から連続して使う。
    static constexpr std::uint32_t kSrvCount = 3;

    TaaPass();
    ~TaaPass();
    TaaPass(const TaaPass&) = delete;
    TaaPass& operator=(const TaaPass&) = delete;

    // PSO・ルート署名と、履歴・解決後の入れ物を作る。資源は Resize で作る。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex);

    // 資源を作り直す(履歴は捨てる)。呼ぶ前に GPU が使い終えていること。
    bool Resize(std::uint32_t width, std::uint32_t height);

    bool Ready(std::uint32_t width, std::uint32_t height) const;

    // 本編の深度の SRV 番号(呼び出し側が、深度バッファの SRV をここへ書く)。
    std::uint32_t DepthSrvIndex() const;

    // current(シーンの HDR。ShaderResource へ遷移させる)と履歴から解決し、解決後の HDR を Resolved() に作る。
    // 解決後の HDR は RenderTarget 状態で返る(続けてレンズフレアを足せる)。履歴へもコピーする。
    // 本編の深度は、呼び出し側が読み取り状態にしてから呼ぶ。
    void Record(const rs::rhi::CommandList& commandList, SceneHdrTarget& current, const TaaFrameParams& params);

    SceneHdrTarget& Resolved();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
