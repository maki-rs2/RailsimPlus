//
// IblGenerator.h - IBL(画像ベースの環境光)の生成と保持
//
// 環境ソースキューブ(空+地面の放射輝度)→ 拡散キューブ(放射照度÷π)→ 鏡面キューブ(ミップ付き)と、
// BRDF LUT(起動後に1回だけ)を作る。キューブは太陽の高度が閾値以上動いたフレームに、1フレームで
// 全部作り直す。生成はすべてRTVへのフルスクリーン描画(compute UAVは使わない)。
// 大気LUT(AtmosphereLuts)の生成が済んだ後に呼ぶ(環境ソースはSky-View LUTを引く)。
//
// 暗順応: 環境の生成のたびに、大気の空だけの上半球平均から順応の照度 E と
// 露出の倍率を1x1へ描く(ExposureTarget。SRVは固定枠の末尾の別の1枠)。
//
// SRVは共有SRVヒープの連続した5枠(先頭から 環境ソースキューブ・拡散キューブ・鏡面キューブ・
// BRDF LUT・大気の空だけの上半球平均)。先頭の枠は、大気LUTの3枠のすぐ後ろであること(SkyPassのデバッグ経路が、大気LUTの
// 先頭から4枠のテーブルで環境ソースを t3 として引くため)。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../../core/Math.h"
#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "AtmosphereLuts.h"
#include "EnvironmentState.h"
#include "LandscapeContrast.h"

namespace rs::render {

struct RenderTuning;

// 環境ソースへ重ねる標準の空の遠景(LandscapeCube)の入力。
struct IblLandscape {
    // 遠景キューブ(SRV状態)のSRVのGPUハンドル。0なら遠景を重ねない(サムネイル・プラグインの空)。
    std::uint64_t srvGpuHandle = 0;
    // 画像の明暗の幅の統計(LandscapeCube::ContrastStats)。
    LandscapeContrastStats contrast;
};

// 環境ソースの地平線より上へ入れる写真の空の層の入力(Envの鏡面キューブ。標準の空のときだけ)。
// 判定(使うか)は EnvironmentState::photoSky の1か所で、ここには結果だけを渡す。
struct IblPhotoSky {
    // Envの鏡面キューブ(SRV状態)のSRVのGPUハンドル。0なら写真の空の層なし。
    std::uint64_t specularCubeSrvGpuHandle = 0;
    // 鏡面キューブの中身の識別(EnvCubeSet::ContentSerial)。Envの切り替えで変わったら作り直す。
    std::uint64_t cubeSerial = 0;
    float skyMean[3] = {0.0f, 0.0f, 0.0f};            // 写真の上半球平均(リニア)
    bool sunFound = false;
    float sunDirection[3] = {0.0f, 1.0f, 0.0f};        // 写真の座標の太陽の方向
    YawRotation yaw;                                    // 実際の太陽の方位を写真の太陽の方位へ重ねる回転
    bool fullSphere = false;                            // 写真を全方向に使い、遠景と大気の地面を重ねない
};

// 環境ソースへ入れる月明かりの空の入力(標準の空のときだけ。EnvironmentState::moonDirection/moonRatio)。
struct IblMoon {
    rs::core::Vec3 directionToMoon = rs::core::Vec3(0.0f, 1.0f, 0.0f); // 観測者から見た月の方向
    float ratio = 0.0f; // 太陽の放射照度に対する月の放射照度の比。0なら月なし
};

class IblGenerator {
public:
    // 連続して使うSRVの枠数(環境ソース・拡散・鏡面・BRDF LUT・大気の空だけの上半球平均)。
    static constexpr std::uint32_t kSrvCount = 5;
    // 5枠の並び(先頭からの相対位置)。
    static constexpr std::uint32_t kEnvSourceSrvOffset = 0;
    static constexpr std::uint32_t kDiffuseSrvOffset = 1;
    static constexpr std::uint32_t kSpecularSrvOffset = 2;
    static constexpr std::uint32_t kBrdfLutSrvOffset = 3;
    static constexpr std::uint32_t kSkyMeanSrvOffset = 4;  // 1x1のキューブ(+X面だけ使う)

    IblGenerator();
    ~IblGenerator();
    IblGenerator(const IblGenerator&) = delete;
    IblGenerator& operator=(const IblGenerator&) = delete;

    // PSO・ルート署名・資源を作る。device・srvHeap は使い終わるまで生きていること。
    // firstSrvIndex から kSrvCount 枠をSRVに使う。exposureSrvIndex は暗順応の結果(1x1)のSRVの1枠
    // (固定の枠の末尾に別に取る。kSrvCount の並びには含めない)。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex,
              std::uint32_t exposureSrvIndex);

    bool Ready() const;

    // 初回、および sunDirectionToSun(観測者から見た太陽の方向)の高度が前回生成時から
    // tuning.iblRegenerateThresholdDeg 以上変わったとき、調整値の版(tuning のアドレス)が変わったときに、
    // キューブ3枚を作り直す(BRDF LUTは初回だけ)。
    // 暗順応(上半球平均と倍率)だけは、作り直さないフレームも毎回今の太陽・月で更新する(倍率を階段にしないため)。
    // 環境ソースの明るさは tuning.skyRadianceScale(背景の空と同じ)。呼ぶたびに必ず記録するので true
    // (RTV・ビューポート・シザーを書き換えているため、呼び出し側がシーン描画の前に張り直すこと)。
    // 4つとも、SRV状態へ戻してから返る。luts は Update 済みであること。
    // landscape: 標準の空の遠景。srvGpuHandle が0以外なら環境ソースへ遠景を重ねる(調整値は tuning の landscape*)。
    // 0(サムネイル・プラグインの空)なら重ねない。前回の生成と遠景の有無が変わったときも、太陽が動かなくても作り直す。
    // photoSky: 写真の空の層(既定は層なし)。層の有無・鏡面キューブの識別・写真の平均色が前回と
    // 変わったときも作り直す。方位合わせの回転は太陽の方位で変わるが、太陽の変化での再生成にまかせる。
    // moon: 月明かりの空(luts の月用Sky-View LUTを引く)。方向が tuning.iblRegenerateThresholdDeg 以上動いた・
    // 比が前回から相対で kMoonIblRegenerateRatioChange 以上変わった・0との行き来があったときも作り直す
    // (AtmosphereMath::ShouldRegenerateIblForMoon)。既定は月なし。
    bool Update(const rs::rhi::CommandList& commandList, const AtmosphereLuts& luts,
                const rs::core::Vec3& sunDirectionToSun, const RenderTuning& tuning,
                const IblLandscape& landscape = IblLandscape{}, const IblPhotoSky& photoSky = IblPhotoSky{},
                const IblMoon& moon = IblMoon{});

    // SRV枠の先頭(環境ソースキューブ)のGPUハンドル。
    std::uint64_t SrvTableGpuHandle() const;

    // メッシュのシェーダが ResourceDescriptorHeap[] で引くための、共有SRVヒープ内の絶対番号。
    // Init 前は0。
    std::uint32_t DiffuseSrvIndex() const;
    std::uint32_t SpecularSrvIndex() const;
    std::uint32_t BrdfLutSrvIndex() const;
    // 暗順応の結果(幅2の2画素。ExposureTarget の説明を参照。x=露出へ掛ける倍率、y=順応の照度 E)のSRVのGPUハンドル。
    // 空パス(SkyPass)が画素0を読む。Update のたびに画素0が更新され、1度も記録していない間は中身が0なので、
    // 読む側は max(x, 1) で扱う。
    std::uint64_t ExposureSrvGpuHandle() const;
    // 同じSRVの共有SRVヒープ内の絶対番号(メッシュのシェーダが ResourceDescriptorHeap[] で引く)。Init 前は0。
    std::uint32_t ExposureSrvIndex() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
