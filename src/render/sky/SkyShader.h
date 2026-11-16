//
// SkyShader.h - 空パス用 HLSL シェーダ文字列(大気LUTを視線方向で引く)
//
// 共通部(LUTの引き方・定数)は AtmosphereShader.h と共有する。出力はリニアのHDR値
// (sRGBへの変換はトーンマップ側)。夜の青の定数だけsRGBで持ち、リニアへ変換して使う。
//
#pragma once

#include <string>

#include "../pipeline/ShaderColor.h"
#include "AtmosphereShader.h"
#include "SunColorShader.h"

namespace rs::render {

inline constexpr const char* kSkyConstantsShader = R"HLSL(
// b0(32 DWORD)。float3+floatの組を1レジスタに詰める。SkyPassParamsと同じ並び。
cbuffer SkyConstants : register(b0) {
    float3 gCameraRight;   float gTanHalfFovY;
    float3 gCameraUp;      float gAspect;
    float3 gCameraForward; float gNightBlue;
    float3 gSunToSun;      float gSkyRadianceScale;
    float gSunDiskCosOuter;
    float gSunDiskCosInner;
    float gSunDiskIntensity;
    float gUseEnvCube;
    float3 gMoonToMoon; float gMoonRatio;
    float4 gAdaptation;   // x=暗順応の倍率を掛けるか(0/1)。yzwは詰め物
    float3 gMoonDiskToMoon; float gMoonDiskEnable;
};
)HLSL";

inline constexpr const char* kSkyBodyShader = R"HLSL(
// 夜の空の最低値(暗い青)。天頂と地平線の色を、視線の高度角で補間する。
float3 NightSkyColor(float elevationSin) {
    float g = saturate(asin(clamp(elevationSin, -1.0, 1.0)) * (2.0 / kPi));
    float3 zenith = float3(0.02, 0.03, 0.08);
    float3 horizon = float3(0.05, 0.06, 0.10);
    return LinearFromSrgb(lerp(horizon, zenith, g));
}

TextureCube<float4> gEnvCube : register(t3);
// 追加テーブル(t4)の月用のSky-View LUT(月が太陽と同じ放射照度だったときの空)。gMoonRatio > 0 のときだけ引く。
Texture2D<float4> gMoonSkyViewLut : register(t4);
// 追加テーブル(t5)の暗順応の1x1(x=倍率)。gAdaptation.x > 0.5 のときだけ引く。
Texture2D<float4> gExposureTex : register(t5);

// 円盤の縁をなめらかに落とし始める半径(月の角半径に対する比)。
static const float kMoonDiskInnerRadiusRatio = 0.9;

// 月の円盤の放射輝度(大気の透過の前。円盤の外は0)。月を球とみなし、視線が当たる点の法線から
// Lommel-Seeliger の反射(L ∝ μ0/(μ0+μ)。μ0=太陽の入射角のcos、μ=観測者への出射角のcos)で陰影を付ける。
// 満月(μ0=μ)では円盤が一様になり、円盤全体の放射照度が kFullMoonToSunIrradianceRatio × 太陽の放射照度になるよう明るさを決める
// (円盤の立体角は π sin²(角半径))。太陽は月から見ても地球から見ても同じ方向とみなす。
float3 MoonDiskRadiance(float3 dir, float3 moonDir, float3 sunDir) {
    float sinRadius = sin(kMoonAngularRadius);
    float3 offset = (dir - moonDir * dot(dir, moonDir)) / sinRadius;  // 円盤の上の位置(月の半径を1とする)
    float r = length(offset);
    if (r >= 1.0 || dot(dir, moonDir) <= 0.0) return 0.0;
    float mu = sqrt(1.0 - r * r);
    float3 normal = offset - moonDir * mu;  // 観測者を向いた半球の点の法線(単位ベクトル)
    float mu0 = dot(normal, sunDir);
    if (mu0 <= 0.0) return 0.0;
    float fullRadiance = kFullMoonToSunIrradianceRatio * kSunIrradiance / (kPi * sinRadius * sinRadius);
    float edge = 1.0 - smoothstep(kMoonDiskInnerRadiusRatio, 1.0, r);
    return fullRadiance * (2.0 * mu0 / (mu0 + mu)) * edge;
}

float4 PSMain(VSOut i) : SV_TARGET {
    float3 dir = normalize(gCameraForward +
                           i.ndc.x * gTanHalfFovY * gAspect * gCameraRight +
                           i.ndc.y * gTanHalfFovY * gCameraUp);
    float3 sunDir = normalize(gSunToSun);

    bool hitsGround;
    // 地平線より下は、LUTの地面色(灰褐色)でなく地平線すれすれの空の色にする(地形の端と地平線の間に帯が見えないようにするため)。
    float3 sky = SkyRadianceWithMoon(gMoonSkyViewLut, dir, sunDir, normalize(gMoonToMoon), gMoonRatio,
                                     gSkyRadianceScale, true, hitsGround);
    // 開発用の確認手段(RS_DEBUG_SKY_FROM_ENVCUBE=1)。環境ソースキューブには焼き込み時に同じ倍率が
    // 掛かっていて、遠景も焼かれている(背景には遠景が無いので、遠景の部分は一致しない)。円盤と夜の青は下で足す。
    if (gUseEnvCube > 0.5) sky = gEnvCube.SampleLevel(gLinearClamp, dir, 0).rgb;

    // 太陽の円盤。色は観測者から太陽方向への透過率(正午は白、夕方は赤橙)。地面の向こうでは描かない。
    float3 sunTransmittance = SunColorFromTransmittance(gTransmittanceLut, gLinearClamp, sunDir);
    float disk = smoothstep(gSunDiskCosOuter, gSunDiskCosInner, dot(dir, sunDir));
    disk = hitsGround ? 0.0 : disk;

    float3 color = sky + sunTransmittance * (gSunDiskIntensity * disk);
    // 月の円盤。色は観測者から月の方向への透過率(地平線近くでは赤みを帯びる)。地面の向こうでは描かない。
    if (gMoonDiskEnable > 0.5 && !hitsGround) {
        float3 moonDir = normalize(gMoonDiskToMoon);
        float3 moonTransmittance = SunColorFromTransmittance(gTransmittanceLut, gLinearClamp, moonDir);
        color += moonTransmittance * MoonDiskRadiance(dir, moonDir, sunDir);
    }
    float3 night = NightSkyColor(dir.y) * saturate(gNightBlue);
    // 標準の空の暗順応(プリ露出): 倍率を掛ける。IBLの生成前(中身が0)は1。
    float gain = gAdaptation.x > 0.5 ? max(gExposureTex.Load(int3(0, 0, 0)).x, 1.0) : 1.0;
    return float4(max(color, night) * gain, 1.0);
}
)HLSL";

inline std::string BuildSkyShader() {
    return BuildAtmosphereCommonShader() + kSunColorShader + kSrgbTransferShader + kSkyConstantsShader +
           kSkyBodyShader;
}

} // namespace rs::render
