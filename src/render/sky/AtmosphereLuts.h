//
// AtmosphereLuts.h - 大気LUT(Hillaire 2020)の生成と保持
//
// 透過率LUT・多重散乱LUT・Sky-View LUTの3枚を持つ。生成はすべてRTVへのフルスクリーン描画で、
// compute UAVは使わない。LUT同士の依存
// (多重散乱とSky-Viewが透過率を、Sky-Viewが多重散乱を引く)は共有SRVヒープのSRVを通す。
// LUTの入れ物は固定サイズのSceneHdrTarget(形式・状態の追跡が同じ)を再利用する。
//
// 3枚のSRVは共有SRVヒープの連続した3枠(先頭から 透過率・多重散乱・Sky-View)。
// Sky-View LUTは、太陽の高度と観測者の高さに依存する(UVの割り当ては基準の高さ kObserverHeightKm で固定)。
// このほかに、月用のSky-View LUT(太陽用と同じシェーダ・大きさ。「月が太陽と同じ放射照度だったとき」の空)を
// 別の1枠に持つ(月明かりの空。使う側が moonRatio を掛ける)。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../../core/Math.h"
#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "AtmosphereParams.h"

namespace rs::render {

class AtmosphereLuts {
public:
    // LUTの枚数(=連続して使うSRVの枠数。先頭から 透過率・多重散乱・Sky-View)。
    static constexpr std::uint32_t kSrvCount = 3;

    AtmosphereLuts();
    ~AtmosphereLuts();
    AtmosphereLuts(const AtmosphereLuts&) = delete;
    AtmosphereLuts& operator=(const AtmosphereLuts&) = delete;

    // PSO・ルート署名・LUTの資源を作る。device・srvHeap は使い終わるまで生きていること。
    // firstSrvIndex から3枠(透過率・多重散乱・Sky-View)、moonSrvIndex の1枠(月用のSky-View)をSRVに使う。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
              std::uint32_t firstSrvIndex, std::uint32_t moonSrvIndex);

    bool Ready() const;

    // 最初の呼び出しで透過率・多重散乱LUTを生成し、以後は sunDirectionToSun(観測者から見た
    // 太陽の方向)が前回生成時から閾値以上変わったときだけSky-View LUTを作り直す。
    // 何か記録したら true(RTV・ビューポート・シザーを書き換えているため、呼び出し側が
    // シーン描画の前に張り直すこと)。LUTはどれもSRV状態へ戻してから返る。
    // moonRatio(太陽の放射照度に対する月の放射照度の比)が0より大きいときだけ、月用のSky-View LUTを
    // 同じ閾値で作り直す(0のあいだは作らない。引く側も0を掛けるので古い中身のままでよい)。
    // moonDirectionToMoon は観測者から見た月の方向。
    // observerHeightKm は、Sky-View LUT(太陽用・月用)を積分する観測者の高さ[km、地表から]。前回の生成時から
    // kObserverHeightRegenerateKm 以上変わったときも作り直す。省略すると基準の高さ(サムネイル等、追従しない呼び出し)。
    bool Update(const rs::rhi::CommandList& commandList, const rs::core::Vec3& sunDirectionToSun,
                const rs::core::Vec3& moonDirectionToMoon = rs::core::Vec3(0.0f, 1.0f, 0.0f),
                float moonRatio = 0.0f, float observerHeightKm = atmosphere::kObserverHeightKm);

    // 3枚のSRV(先頭=透過率、連続して多重散乱・Sky-View)のGPUハンドルと、そのヒープ。
    std::uint64_t SrvTableGpuHandle() const;
    const rs::rhi::SrvHeap& SrvHeapRef() const;

    // 月用のSky-View LUT(SRV状態)のGPUハンドル。未生成でも有効なSRV(中身は引かれない前提)。
    std::uint64_t MoonSkyViewSrvGpuHandle() const;

    // 透過率LUTの共有SRVヒープ内の絶対番号(メッシュのシェーダが ResourceDescriptorHeap[] で引く)。
    // Init 前は0。
    std::uint32_t TransmittanceSrvIndex() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
