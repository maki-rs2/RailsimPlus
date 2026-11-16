//
// LuminanceMeterPass.h - フォトモードの ISO オート用の測光(シーンの明るさを、小さな格子の対数輝度で測って CPU へ読み戻す)
//
// シーンの HDR を、32x18 の格子へ縮める全画面の描画が1回。出力の1画素が入力の1区画の「輝度の対数(log2)の平均」で、
// 輝度は Rec.709 の係数、1サンプルの値は [kMinLuminance, kMaxLuminance] に収める。結果は小さなバッファへ写し、
// Poll が数回の呼び出しのあとに値を返す(ゲームの描画は読み戻しを待たない。DepthProbe と同じフレーム数で待つ方式)。
// 読み戻し待ちの間は描画も写しも記録しないので、測光は数フレームに1回になる。
// 中央重みの平均と輝度への戻しは CPU 側(app/camera の MeteredLuminance)が行う。
// 入力は ShaderResource へ遷移させ、ビューポートとシザーは入力の大きさへ戻して返る。
// 資源は固定の大きさで、CreateResources が1回だけ作る。DX12 の型は .cpp に閉じる。
//
#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "SceneHdrTarget.h"

namespace rs::render {

struct LuminanceMeterGrid {
    static constexpr std::uint32_t kWidth = 32;
    static constexpr std::uint32_t kHeight = 18;
    std::array<float, static_cast<std::size_t>(kWidth) * kHeight> logLuminance{}; // 行ごとに左から。log2(輝度)の区画平均
};

class LuminanceMeterPass {
public:
    // SRV 枠の数(出力の1枚)。
    static constexpr std::uint32_t kSrvCount = 1;
    // 1サンプルの輝度の範囲(測光が取りうる輝度の下限・上限)。
    static constexpr float kMinLuminance = 1.0e-4f;
    static constexpr float kMaxLuminance = 64.0f;
    // 写してから取り出すまでに待つ Poll の回数。この間は次の測光を始めない。
    static constexpr int kReadbackDelayPolls = 4;

    LuminanceMeterPass();
    ~LuminanceMeterPass();
    LuminanceMeterPass(const LuminanceMeterPass&) = delete;
    LuminanceMeterPass& operator=(const LuminanceMeterPass&) = delete;

    // PSO・ルート署名と、出力の入れ物を作る。資源は CreateResources で作る。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex);

    // 出力の資源と読み戻しのバッファを作る(作り済みなら何もしない)。呼ぶ前に GPU がこのパスを使っていないこと。
    bool CreateResources();

    bool Ready() const;

    // input(シーンの HDR)を測り、結果を読み戻し用のバッファへ写す。読み戻し待ちの間は何もしない。
    void Record(const rs::rhi::CommandList& commandList, SceneHdrTarget& input);

    // 写したあと、GPU が書き終えるまでの数回の呼び出しを数え、揃ったら格子を返す。
    bool Poll(LuminanceMeterGrid& grid);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
