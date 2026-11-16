//
// SkyPass.h - 大気LUT(Hillaire 2020)を視線方向で引く空パス
//
// 画面の各ピクセルの視線方向から Sky-View LUT を引き、
// 太陽の円盤(透過率LUTの色×調整値)と、夜の青の最低値(標準の空の暗順応が有効なときは足さない)を足す。
// プラグインの空も同じ計算で描く(円盤・夜の青・月は呼び出し側が0にし、明るさの倍率だけ変える。天体は SkyBodyPass が足す)。
// LUTの生成は AtmosphereLuts が行う。
// 出力はリニアのHDR値(sRGBへの変換はトーンマップ側)。
//
// 不透明パス・WBOIT合成より前に、深度テスト/書き込みともにOFFで全画面
// (SV_VertexIDのみで3頂点生成するfullscreen-triangle)描画する。
// 描画先は常に kSceneHdrFormat(HDR中間RTの形式)。
//
#pragma once

#include <memory>

#include "../../core/Math.h"
#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "AtmosphereLuts.h"

namespace rs::render {

struct RenderTuning;

// 空パスのルート定数(HLSLの SkyConstants と同じ並び、32 DWORD)。
struct SkyPassParams {
    float cameraRight[3] = {1.0f, 0.0f, 0.0f};
    float tanHalfFovY = 0.5f;
    float cameraUp[3] = {0.0f, 1.0f, 0.0f};
    float aspect = 1.0f;
    float cameraForward[3] = {0.0f, 0.0f, 1.0f};
    // 夜の青(空の最低値)を足す強さ(0〜1)。MakeSkyPassParams は 1 - dayAlpha を入れる。標準の空の暗順応が有効なとき
    // (EnvironmentState::adaptation)は、呼び出し側が0にする(星明かりと月明かりが夜の空を受け持つ)。
    float nightBlue = 0.0f;
    float sunToSun[3] = {0.0f, 1.0f, 0.0f};
    float skyRadianceScale = 1.0f;
    float sunDiskCosOuter = 0.99999f;
    float sunDiskCosInner = 0.999995f;
    float sunDiskIntensity = 30.0f;
    // 1のとき、背景を Sky-View LUT でなく環境ソースキューブ(t3)から引く(開発用の確認手段)。
    float useEnvCube = 0.0f;
    // 月明かりの空。moonRatio(太陽の放射照度に対する月の放射照度の比、EnvironmentState::moonRatio)が0のときは
    // 月のSky-View LUTを引かない。moonToMoon は観測者から見た月の方向(単位ベクトル)。
    float moonToMoon[3] = {0.0f, 1.0f, 0.0f};
    float moonRatio = 0.0f;
    // 1のとき、出力(大気の空・星明かり・太陽の円盤)へ暗順応の倍率(プリ露出)を掛ける。倍率は Record の
    // exposureSrvGpuHandle の1x1のx成分(1未満は1)。標準の空の暗順応(EnvironmentState::adaptation)が有効なときだけ1にする。
    float useAdaptation = 0.0f;
    float adaptationPad[3] = {0.0f, 0.0f, 0.0f};  // float4にそろえる詰め物
    // 月の円盤。moonDiskEnable=1 のとき、moonDiskToMoon(観測者から見た月の方向)に月の円盤を描く
    // (EnvironmentState::moonDiskVisible・moonDiskDirection)。満ち欠けは太陽の方向(sunToSun)からの陰影で出す。
    float moonDiskToMoon[3] = {0.0f, 1.0f, 0.0f};
    float moonDiskEnable = 0.0f;
};

// ビュー行列(row-vector、LookAt由来)と視野角から、視線方向の再構成に要る値を詰める。
// sunDirectionToSun: 観測者から見た太陽の方向(FrameRenderer::sunDirection_ は光が進む向きなので、
// 呼び出し側で符号反転すること)。dayAlpha: rs::world::ComputeDayAlpha由来の昼夜係数
// (1=昼、0=完全な夜)。調整値は tuning(skyRadianceScale・sunDiskIntensity・sunDiskAngularDiameterDeg)。
SkyPassParams MakeSkyPassParams(const rs::core::Mat4& view, float fovYRadians, float aspectRatio,
                                const rs::core::Vec3& sunDirectionToSun, float dayAlpha,
                                const RenderTuning& tuning);

class SkyPass {
public:
    SkyPass();
    ~SkyPass();
    SkyPass(const SkyPass&) = delete;
    SkyPass& operator=(const SkyPass&) = delete;

    bool Init(const rs::rhi::Device& device);

    // luts のSRV(透過率・多重散乱・Sky-View)と、月用のSky-View LUT(別テーブル)と、その直後の4枠目(環境ソースキューブ。IblGenerator が
    // 大気LUTの直後に取る)をバインドして全画面を描く。luts は
    // SRV状態(Update済み)であること。呼び出し側がHDRシーンターゲットをバインド済みの前提。
    // exposureSrvGpuHandle: 暗順応の1x1(IblGenerator::ExposureSrvGpuHandle)。params.useAdaptation=1 のときだけ引く。
    // 0なら大気の透過率LUT(同じ2Dテクスチャの型)で代用する(引かれない)。
    void Record(const rs::rhi::CommandList& commandList, const AtmosphereLuts& luts,
                const SkyPassParams& params, std::uint64_t exposureSrvGpuHandle = 0) const;

    bool Ready() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
