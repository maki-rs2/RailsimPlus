//
// IblBakeConstants.h - IBL生成(IblGenerator.cpp)のルート定数の並び(HLSLのcbufferと同じ)
//
// 内部用ヘッダ。IblShader.h の EnvBakeConstants / FilterConstants と同じ並びで、変えるときは両方。
//
#pragma once

#include <cstdint>

namespace rs::render::ibl_bake {

// ルート定数(HLSLの EnvBakeConstants / FilterConstants と同じ並び)。
struct EnvBakeConstants {
    std::uint32_t face = 0;
    float sunToSun[3] = {0.0f, 1.0f, 0.0f};
    float radianceScale = 1.0f;
    float useLandscape = 0.0f;
    float landscapeYawRad = 0.0f;
    float landscapeBrightness = 1.0f;
    float landscapeTint[3] = {1.0f, 1.0f, 1.0f};
    float landscapeSaturation = 1.0f;
    float landscapeContrast = 1.0f;
    float landscapeContrastPivot = 0.18f;
    float landscapeContrastScale = 1.0f;
    float pad = 0.0f; // HLSL の cbuffer で次の float3 がレジスタの先頭から始まるための詰め物
    float photoSkyMean[3] = {0.0f, 0.0f, 0.0f};
    float photoSunFound = 0.0f;
    float photoSunDirection[3] = {0.0f, 1.0f, 0.0f};
    float usePhoto = 0.0f;
    float photoYawCos = 1.0f;
    float photoYawSin = 0.0f;
    float moonRatio = 0.0f; // 月の放射照度の比(0=月なし)。photoYaw と同じレジスタに詰める
    float moonPad = 0.0f;
    float moonToMoon[3] = {0.0f, 1.0f, 0.0f};
    float moonPad2 = 0.0f; // 末尾のレジスタを4 DWORDにそろえる(ルート定数の数をcbufferの大きさに合わせる)
    float adaptStart = 0.0f; // 暗順応(AdaptationPS)。RenderTuning の nightAdaptationStart / Exponent / MaxGain
    float adaptExponent = 0.0f;
    float adaptMaxGain = 1.0f;
    float adaptBoost = 1.0f; // 深夜の上乗せ(NightAdaptationBoost を太陽高度から求めた値)
    float adaptDayStart = 0.0f; // 昼の曲線(AdaptationPS)。RenderTuning の dayAdaptationStart / Exponent
    float adaptDayExponent = 0.0f;
    float adaptPad0 = 0.0f; // 末尾のレジスタを4 DWORDにそろえる
    float adaptPad1 = 0.0f;
};
struct FilterConstants {
    std::uint32_t face = 0;
    float roughness = 0.0f;
    float srcMip = 0.0f; // 環境ソースのミップ作成だけが使う(読むミップ)
};
constexpr unsigned int kBakeConstantDwords = sizeof(EnvBakeConstants) / sizeof(std::uint32_t);
constexpr unsigned int kFilterConstantDwords = sizeof(FilterConstants) / sizeof(std::uint32_t);
static_assert(kBakeConstantDwords == 40 && kFilterConstantDwords == 3, "HLSLのcbufferと同じDWORD数");


} // namespace rs::render::ibl_bake
