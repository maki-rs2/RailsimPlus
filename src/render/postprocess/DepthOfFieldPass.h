//
// DepthOfFieldPass.h - フォトモードの被写界深度(絞り・フォーカス距離に応じた、円盤のぼけ)
//
// シーンの HDR(TAA の解決後、またはそのまま)と本編の深度から、撮影カメラの錯乱円に合わせてぼかした HDR を作る。
// 全画面の描画は7回(半解像度が中心): プリフィルタ → 近景のぼけのタイル最大(横・縦) → 周囲 3x3 のタイルの最大 →
// 遠景のギャザー・近景のギャザー → 合成(DepthOfFieldShader.h)。ぼけは HDR のリニアのまま掛ける。
// 結果は Output() に作り、続くモーションブラー・レンズフレア・ブルーム・トーンマップの入力になる。
// 作業用の RT は SceneHdrTarget。SRV 枠は呼び出し側が渡す連続枠の先頭から、プリフィルタ・タイルの途中結果・タイル・
// 周囲・遠景・近景・出力・(本編の深度。呼び出し側が書く)の順に kSrvCount 枠使う。
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
struct DepthOfFieldFrameParams {
    float invViewProj[16] = {};       // 今フレームの(ジッタなしの)viewProj の逆行列
    float focalLengthMm = 35.0f;      // 焦点距離[mm]
    float aperture = 5.6f;            // 絞り(F値)
    float focusDistanceM = 10.0f;     // フォーカス距離[m]。最短撮影距離以上に丸めてあること(ClampFocusDistanceM)
    float frameWidthMm = 36.0f;       // 撮影フレームの幅[mm](出力の縦横比で決まる。PhotoFrameForAspect)
    float maxRadiusFraction = 0.015f; // ぼけの半径の上限(出力の幅に対する割合)
    int samples = 32;                 // 円盤の中で拾う点の数
    std::uint32_t frameIndex = 0;     // ノイズの位相
};

class DepthOfFieldPass {
public:
    // SRV 枠の数(プリフィルタ・タイルの途中結果・タイル・周囲・遠景・近景・出力・本編の深度)。
    static constexpr std::uint32_t kSrvCount = 8;

    DepthOfFieldPass();
    ~DepthOfFieldPass();
    DepthOfFieldPass(const DepthOfFieldPass&) = delete;
    DepthOfFieldPass& operator=(const DepthOfFieldPass&) = delete;

    // PSO・ルート署名と、各 RT の入れ物を作る。資源は Resize で作る。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex);

    // 資源を作り直す。呼ぶ前に GPU が使い終えていること。
    bool Resize(std::uint32_t width, std::uint32_t height);

    bool Ready(std::uint32_t width, std::uint32_t height) const;

    // 本編の深度の SRV 番号(呼び出し側が、深度バッファの SRV をここへ書く)。
    std::uint32_t DepthSrvIndex() const;

    // input(シーンの HDR。ShaderResource へ遷移させる)をぼかして Output() に作る。出力は RenderTarget 状態で返る
    // (続けてモーションブラー・レンズフレアを足せる)。本編の深度は、呼び出し側が読み取り状態にしてから呼ぶ。
    // input は Resize の大きさであること(違えば何もしない)。
    void Record(const rs::rhi::CommandList& commandList, SceneHdrTarget& input, const DepthOfFieldFrameParams& params);

    SceneHdrTarget& Output();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
