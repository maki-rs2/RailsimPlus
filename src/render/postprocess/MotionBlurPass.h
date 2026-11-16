//
// MotionBlurPass.h - モーションブラー(カメラの動き+動く物体の速度のぼかし)
//
// シーンの HDR(TAA の解決後、またはそのまま)と、本編の深度・物体の速度から、速度の向きにぼかした HDR を作る。
// 4 つのパス: 速度の合成 → タイルごとの最大(横・縦) → 周囲 3x3 のタイルの最大 → ぼかし(MotionBlurShader.h)。
// 結果は Output() に作り、続くレンズフレア・ブルーム・トーンマップの入力になる。TAA の履歴には入らない
// (履歴へのコピーはぼかしの前に済ませる)。
// 作業用の RT は SceneHdrTarget。SRV 枠は呼び出し側が渡す連続枠の先頭から、速度・タイルの途中結果・タイル・周囲・
// 出力・(本編の深度。呼び出し側が書く)の順に kSrvCount 枠使う。
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
struct MotionBlurFrameParams {
    float invViewProj[16] = {};  // 今フレームの(ジッタなしの)viewProj の逆行列
    float prevViewProj[16] = {}; // 前フレームの(ジッタなしの)viewProj
    float shutter = 0.5f;        // 1フレームの動きのうち、ぼかしの長さにする割合
    float maxRadiusFraction = 0.02f; // ぼかしの半径の上限(画面の高さに対する割合)
    int samples = 12;            // 速度の向きに拾う点の数
    std::uint32_t frameIndex = 0; // ノイズの位相
    std::uint32_t objectVelocitySrvIndex = 0; // 物体の速度バッファの SRV 番号
};

class MotionBlurPass {
public:
    // SRV 枠の数(速度・タイルの途中結果・タイル・周囲・出力・本編の深度)。
    static constexpr std::uint32_t kSrvCount = 6;

    MotionBlurPass();
    ~MotionBlurPass();
    MotionBlurPass(const MotionBlurPass&) = delete;
    MotionBlurPass& operator=(const MotionBlurPass&) = delete;

    // PSO・ルート署名と、各 RT の入れ物を作る。資源は Resize で作る。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex);

    // 資源を作り直す。呼ぶ前に GPU が使い終えていること。
    bool Resize(std::uint32_t width, std::uint32_t height);

    bool Ready(std::uint32_t width, std::uint32_t height) const;

    // 本編の深度の SRV 番号(呼び出し側が、深度バッファの SRV をここへ書く)。
    std::uint32_t DepthSrvIndex() const;

    // input(シーンの HDR。ShaderResource へ遷移させる)をぼかして Output() に作る。出力は RenderTarget 状態で返る
    // (続けてレンズフレアを足せる)。本編の深度は、呼び出し側が読み取り状態にしてから呼ぶ。
    // input は Resize の大きさであること(違えば何もしない)。
    void Record(const rs::rhi::CommandList& commandList, SceneHdrTarget& input, const MotionBlurFrameParams& params);

    SceneHdrTarget& Output();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
