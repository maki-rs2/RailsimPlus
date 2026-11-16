//
// AerialPerspectiveLut.h - 大気遠近(aerial perspective。AP)のLUTの生成と保持
//
// カメラの視錐台を距離方向に区切り、「カメラ(高さ固定の観測者)から距離 d までの区間で散乱された光(rgb)と、
// その区間の透過率のRGB平均(a)」を持つ。距離は手前を細かく割る(スライス位置の2乗)。
// 距離方向のスライスを横に並べた1枚の2Dテクスチャ(RGBA16F。幅 = スライス数 × タイル、高さ = タイル)で、
// 入れ物は固定サイズのSceneHdrTarget(AtmosphereLuts が透過率LUTに使うのと同じ)。生成はRTVへの
// フルスクリーン描画1回で、compute UAVは使わない。引く側は、隣り合う2スライスを手で線形補間する。
// 大きさ・最大距離は RenderTuning(起動時に決まり、起動後は変えない)。
//
// LUTはカメラの向きに依存するので、毎フレーム作り直す。透過率LUT・多重散乱LUT(AtmosphereLuts)を引く。
// 割り当ての式は AtmosphereMath.cpp と同じ。SRVは共有SRVヒープの1枠。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../../core/Math.h"
#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "AtmosphereLuts.h"
#include "AtmosphereMath.h"

namespace rs::render {

class AerialPerspectiveLut {
public:
    AerialPerspectiveLut();
    ~AerialPerspectiveLut();
    AerialPerspectiveLut(const AerialPerspectiveLut&) = delete;
    AerialPerspectiveLut& operator=(const AerialPerspectiveLut&) = delete;

    // PSO・ルート署名・LUTの資源を作る。大きさは RenderTuning::Embedded()。device・srvHeap は使い終わるまで
    // 生きていること。srvIndex は srvHeap の何番目の枠をLUTのSRVに使うか。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t srvIndex);

    bool Ready() const;

    // 作るかどうか(既定は作る)。偽のあいだは Update が何も記録しない(中身は古いまま)。
    void SetEnabled(bool enabled);
    bool Enabled() const;

    // LUTを作り直す。luts は透過率・多重散乱LUTを生成済みであること(AtmosphereLuts::Update の後に呼ぶ)。
    // sunDirectionToSun は観測者から見た太陽の方向(ワールド、Y-up)、camera はメインの描画のカメラ。
    // 何か記録したら true(RTV・ビューポート・シザーを書き換えているため、呼び出し側がシーン描画の前に張り直すこと)。
    // LUTはSRV状態へ戻してから返る。
    // observerHeightKm は光を積分する観測者の高さ[km、地表から]。
    bool Update(const rs::rhi::CommandList& commandList, const AtmosphereLuts& luts,
                const rs::core::Vec3& sunDirectionToSun, const atmosphere::AerialCameraFrame& camera,
                float observerHeightKm);

    // LUT(SRV状態)のGPUハンドルと、共有SRVヒープ内の絶対番号。Init 前は0。
    std::uint64_t SrvGpuHandle() const;
    std::uint32_t SrvIndex() const;

    // LUTの大きさ。Init 前は0。
    int SliceCount() const;
    int TileSize() const;
    float MaxDistanceKm() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
