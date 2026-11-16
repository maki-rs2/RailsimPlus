//
// BloomPass.h - ブルーム(縮小チェーン+足し戻し)
//
// シーンのHDRから明るい部分(しきい値を超えた分。soft knee でなめらかにつなぐ)だけを切り出し、
// 1/2・1/4…1/64へ縮小して、小さい方から1段ずつ拡大して足し戻したあと、
// 結果をシーンのHDRへ `scene + bloom * strength` で加算する(Unity・UE と同じ形)。
// しきい値以下の部分には何も足さないので、暗い所のコントラストは変わらず、明るい光源のまわりだけが滲む。
// 各段の作業用RTは SceneHdrTarget(Half16)。SRVは連続した kSrvCount 枠を使う。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "SceneHdrTarget.h"

namespace rs::render {

struct BloomParams {
    float strength = 0.5f;  // 最終の加算の倍率
    float threshold = 0.5f; // 切り出しのしきい値(露出後の明るさ)。0 なら全部が対象
    float scatter = 0.7f;   // 拡大で下の段へ混ぜる割合(0..1)。大きいほど広く滲む
    float exposure = 1.0f;  // トーンマップの露出。Karis平均の重みを露出後の明るさで決めるために使う
};

class BloomPass {
public:
    static constexpr std::uint32_t kLevelCount = 6;
    static constexpr std::uint32_t kSrvCount = kLevelCount;

    BloomPass();
    ~BloomPass();
    BloomPass(const BloomPass&) = delete;
    BloomPass& operator=(const BloomPass&) = delete;

    // PSO・ルート署名を作り、各段のRTを初期化する。firstSrvIndex から kSrvCount 枠をSRVに使う。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex);

    // 各段の大きさを scene の大きさ(幅・高さ)から作り直す。呼ぶ前にGPUがこのRT群を使い終えていること。
    bool Resize(std::uint32_t width, std::uint32_t height);

    // PSOと全段のRTがそろっているか。
    bool Ready() const;

    // scene を読み、縮小・拡大の結果を scene へ加算する。終了時の scene は RenderTarget 状態。
    // scene は Resize に渡した大きさであること(違えば何もしない)。ビューポート・シザーは scene の大きさへ張り直す。
    void Record(const rs::rhi::CommandList& commandList, SceneHdrTarget& scene, const BloomParams& params);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
