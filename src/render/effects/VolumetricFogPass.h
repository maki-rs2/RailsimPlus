//
// VolumetricFogPass.h - ボリュメトリックフォグの格子(視錐台を区切った3Dテクスチャ)を作るパス
//
// 影の描画の後に compute で注入と積分を行う。読む側は積分の結果を SRV で引く。
// 資源は Resize で作るので、使わない間は GPU のメモリを使わない。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "../sky/AtmosphereMath.h"

namespace rs::render {

class VolumeTexture;

// 1フレームぶんの入力。
struct VolumetricFogFrameParams {
    float cameraPos[3] = {};
    atmosphere::AerialCameraFrame camera;
    float toSun[3] = {0.0f, 1.0f, 0.0f};
    float maxDistance = 200.0f;
    float anisotropy = 0.0f;
    float sunIntensity = 1.0f;
    float fogDensity = 0.0f;
    float fogHeightScale = 1.0f;
    float fogBaseHeight = 0.0f;
    float fogAlbedo = 1.0f;
    float ambientScale = 1.0f;
    std::uint32_t diffuseCubeSrvIndex = 0;
    std::uint32_t transmittanceSrvIndex = 0;
    std::uint32_t exposureSrvIndex = 0;
    bool adaptationEnabled = false;
    std::uint64_t shadowConstantsAddress = 0; // 影の定数バッファの GPU アドレス
    float viewProj[16] = {};                  // 今フレームの viewProj(ジッタなし)。次のフレームの履歴を引くのに使う
    float historyWeight = 0.0f;               // 履歴を混ぜる割合
    std::uint64_t frameNumber = 0;            // 通し番号。1つ飛んだら、前の回の結果を履歴に使わない
    bool discardHistory = false;              // 真なら履歴を使わない(視点の瞬間移動など)
};

class VolumetricFogPass {
public:
    static constexpr std::uint32_t kSrvCount = 6; // 注入の結果2枚(今と前を交互)・積分の結果(それぞれ SRV・UAV)

    VolumetricFogPass();
    ~VolumetricFogPass();
    VolumetricFogPass(const VolumetricFogPass&) = delete;
    VolumetricFogPass& operator=(const VolumetricFogPass&) = delete;

    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex);
    // 格子の大きさを合わせる。古い資源をその場で解放するので、GPU が使っていない間に呼ぶこと。
    bool Resize(std::uint32_t gridWidth, std::uint32_t gridHeight, std::uint32_t sliceCount);
    bool Ready(std::uint32_t gridWidth, std::uint32_t gridHeight, std::uint32_t sliceCount) const;

    // 影の描画の後に呼ぶ。結果は読む状態で返る。グラフィックスの PSO は差し替わる。
    void Record(const rs::rhi::CommandList& commandList, const VolumetricFogFrameParams& params);

    std::uint32_t IntegratedSrvIndex() const;
    // 最後に書いた注入の結果。
    VolumeTexture& Scatter();
    // 最後の Record が履歴を使ったか。
    bool HistoryUsed() const;
    VolumeTexture& Integrated();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
