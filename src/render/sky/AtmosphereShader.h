//
// AtmosphereShader.h - 大気LUT(Hillaire 2020)の HLSL シェーダ文字列
//
// 共通部(BuildAtmosphereCommonShader)は、LUT生成(AtmosphereLuts)と空パス(SkyPass)が共有する。
// 物理パラメータはAtmosphereParams.hのconstexprから文字列へ埋め込み、CPUとの二重定義を避ける。
// 座標はY-up。Sky-View LUTのUV割り当ては AtmosphereMath.cpp と同じ式(変えるときは両方)。
// 距離の単位はkm。
//
// シェーダのレジスタ割り当て(LUT生成・空パスで共通):
//   t0=透過率LUT、t1=多重散乱LUT、t2=Sky-View LUT、s0=リニアクランプ。
//   RS_USE_MULTISCAT=1 のときだけ多重散乱LUT(t1)を参照する(多重散乱LUT自身の生成では0)。
//
#pragma once

#include <format>
#include <string>

#include "AtmosphereParams.h"
#include "../resource/RenderTuning.h"

namespace rs::render {

// 物理パラメータをHLSLの定数へ展開した文字列。
inline std::string BuildAtmosphereConstantsShader() {
    namespace a = rs::render::atmosphere;
    const RenderTuning& tuning = RenderTuning::Embedded();
    const float aerosolOpticalDepth = tuning.atmosphereAerosolOpticalDepth;
    return std::format(
        "static const float kPi = 3.14159265359;\n"
        "static const float kBottomRadius = {:.9g};\n"
        "static const float kTopRadius = {:.9g};\n"
        "static const float3 kRayleighScattering = float3({:.9g}, {:.9g}, {:.9g});\n"
        "static const float kRayleighScaleHeight = {:.9g};\n"
        "static const float kMieScattering = {:.9g};\n"
        "static const float kMieAbsorption = {:.9g};\n"
        "static const float kMieScaleHeight = {:.9g};\n"
        "static const float kMiePhaseG = {:.9g};\n"
        "static const float3 kOzoneAbsorption = float3({:.9g}, {:.9g}, {:.9g});\n"
        "static const float kOzoneCenterHeight = {:.9g};\n"
        "static const float kOzoneHalfWidth = {:.9g};\n"
        "static const float kGroundAlbedo = {:.9g};\n"
        "static const float kObserverHeight = {:.9g};\n"
        "static const float kSunIrradiance = {:.9g};\n"
        "static const float kSunAngularRadius = {:.9g};\n"
        "static const float kPlanetRadiusOffset = {:.9g};\n"
        "static const float2 kTransmittanceSize = float2({}, {});\n"
        "static const float kMultiScatSize = {};\n"
        "static const float2 kSkyViewSize = float2({}, {});\n"
        "static const int kTransmittanceSteps = {};\n"
        "static const float kMultiScatSteps = {};\n"
        "static const int kMultiScatSqrtSamples = {};\n"
        "static const float kSkyViewMinSteps = {};\n"
        "static const float kSkyViewMaxSteps = {};\n"
        "static const float kNightSkyBaseRadianceRatio = {:.9g};\n"
        "static const float kFullMoonToSunIrradianceRatio = {:.9g};\n"
        "static const float kMoonAngularRadius = {:.9g};\n",
        a::kBottomRadiusKm, a::kTopRadiusKm, a::kRayleighScatteringPerKm[0],
        a::kRayleighScatteringPerKm[1], a::kRayleighScatteringPerKm[2], a::kRayleighScaleHeightKm,
        a::MieScatteringPerKm(aerosolOpticalDepth), a::MieAbsorptionPerKm(aerosolOpticalDepth), a::kMieScaleHeightKm, a::kMiePhaseG,
        a::kOzoneAbsorptionPerKm[0], a::kOzoneAbsorptionPerKm[1], a::kOzoneAbsorptionPerKm[2],
        a::kOzoneCenterHeightKm, a::kOzoneHalfWidthKm, a::kGroundAlbedo, a::kObserverHeightKm,
        a::kSunIrradiance, tuning.SunAngularRadiusRad(), a::kPlanetRadiusOffsetKm, a::kTransmittanceLutWidth,
        a::kTransmittanceLutHeight, a::kMultiScatLutSize, a::kSkyViewLutWidth, a::kSkyViewLutHeight,
        a::kTransmittanceSteps, a::kMultiScatSteps, a::kMultiScatSqrtSamples, a::kSkyViewMinSteps,
        a::kSkyViewMaxSteps, a::kNightSkyBaseRadianceRatio, a::kFullMoonToSunIrradianceRatio,
        a::kMoonAngularRadiusRad);
}

// 透過率LUTのUV割り当て(リソース宣言なしの純関数)。LUT生成・空パス・メッシュのシェーダが
// 同じ式を使う。定数部(BuildAtmosphereConstantsShader)の後ろへ置く。
inline constexpr const char* kTransmittanceUvShader = R"HLSL(
float2 TransmittanceParamsToUv(float viewHeight, float viewZenithCos) {
    float h = sqrt(max(0.0, kTopRadius * kTopRadius - kBottomRadius * kBottomRadius));
    float rho = sqrt(max(0.0, viewHeight * viewHeight - kBottomRadius * kBottomRadius));
    float discriminant = viewHeight * viewHeight * (viewZenithCos * viewZenithCos - 1.0) +
                         kTopRadius * kTopRadius;
    float d = max(0.0, -viewHeight * viewZenithCos + sqrt(max(0.0, discriminant)));
    float dMin = kTopRadius - viewHeight;
    float dMax = rho + h;
    return float2((d - dMin) / (dMax - dMin), rho / h);
}
)HLSL";

// LUTの引き方・媒質・散乱の積分。
inline constexpr const char* kAtmosphereFunctionsShader = R"HLSL(
SamplerState gLinearClamp : register(s0);
Texture2D<float4> gTransmittanceLut : register(t0);
Texture2D<float4> gMultiScatLut : register(t1);
Texture2D<float4> gSkyViewLut : register(t2);

struct VSOut { float4 pos : SV_POSITION; float2 ndc : TEXCOORD0; };

VSOut VSMain(uint vid : SV_VertexID) {
    VSOut o;
    float2 p = float2((vid << 1) & 2, vid & 2); // (0,0),(2,0),(0,2)
    float2 ndc = p * float2(2, -2) + float2(-1, 1);
    o.pos = float4(ndc, 0, 1);
    o.ndc = ndc;
    return o;
}

// 0..1とLUTの端のテクセル中心の内側(0.5/解像度..1-0.5/解像度)の相互変換。
float SubUvFromUnit(float u, float resolution) { return (u * (resolution - 1.0) + 0.5) / resolution; }
float UnitFromSubUv(float u, float resolution) { return (u * resolution - 0.5) / (resolution - 1.0); }

// 原点中心・半径radiusの球との、視線の手前側の交点までの距離(交点なし・後方は-1)。
float RaySphereNearest(float3 origin, float3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) return -1.0;
    float root = sqrt(discriminant);
    float t0 = -b - root;
    float t1 = -b + root;
    if (t0 < 0.0 && t1 < 0.0) return -1.0;
    if (t0 < 0.0) return max(0.0, t1);
    if (t1 < 0.0) return max(0.0, t0);
    return max(0.0, min(t0, t1));
}

// ---- 透過率LUT(横=視線の向き、縦=高度) ----
void UvToTransmittanceParams(float2 uv, out float viewHeight, out float viewZenithCos) {
    float h = sqrt(max(0.0, kTopRadius * kTopRadius - kBottomRadius * kBottomRadius));
    float rho = h * uv.y;
    viewHeight = sqrt(rho * rho + kBottomRadius * kBottomRadius);
    float dMin = kTopRadius - viewHeight;
    float dMax = rho + h;
    float d = dMin + uv.x * (dMax - dMin);
    viewZenithCos = (d == 0.0) ? 1.0 : (h * h - rho * rho - d * d) / (2.0 * viewHeight * d);
    viewZenithCos = clamp(viewZenithCos, -1.0, 1.0);
}

float3 TransmittanceToSun(float3 pos, float3 sunDir) {
    float height = length(pos);
    float sunCosZenith = dot(sunDir, pos / height);
    float2 uv = TransmittanceParamsToUv(height, sunCosZenith);
    return gTransmittanceLut.SampleLevel(gLinearClamp, uv, 0).rgb;
}

// ---- Sky-View LUT(横=太陽との方位差、縦=視線の高度角。地平線付近へ解像度を寄せる) ----
void UvToSkyViewParams(float2 uv, float viewHeight, out float viewZenithCos, out float lightViewCos) {
    uv = float2(UnitFromSubUv(uv.x, kSkyViewSize.x), UnitFromSubUv(uv.y, kSkyViewSize.y));
    float vHorizon = sqrt(max(0.0, viewHeight * viewHeight - kBottomRadius * kBottomRadius));
    float beta = acos(vHorizon / viewHeight);
    float zenithHorizonAngle = kPi - beta;
    if (uv.y < 0.5) {
        float coord = 1.0 - 2.0 * uv.y;
        coord = 1.0 - coord * coord;
        viewZenithCos = cos(zenithHorizonAngle * coord);
    } else {
        float coord = uv.y * 2.0 - 1.0;
        coord *= coord;
        viewZenithCos = cos(zenithHorizonAngle + beta * coord);
    }
    lightViewCos = -(uv.x * uv.x * 2.0 - 1.0);
}

float2 SkyViewParamsToUv(bool intersectGround, float viewZenithCos, float lightViewCos, float viewHeight) {
    float vHorizon = sqrt(max(0.0, viewHeight * viewHeight - kBottomRadius * kBottomRadius));
    float beta = acos(vHorizon / viewHeight);
    float zenithHorizonAngle = kPi - beta;
    float zenithAngle = acos(clamp(viewZenithCos, -1.0, 1.0));
    float2 uv;
    if (!intersectGround) {
        float coord = 1.0 - zenithAngle / zenithHorizonAngle;
        coord = 1.0 - sqrt(max(coord, 0.0));
        uv.y = coord * 0.5;
    } else {
        float coord = (zenithAngle - zenithHorizonAngle) / beta;
        uv.y = sqrt(max(coord, 0.0)) * 0.5 + 0.5;
    }
    uv.x = sqrt(max(-lightViewCos * 0.5 + 0.5, 0.0));
    return float2(SubUvFromUnit(uv.x, kSkyViewSize.x), SubUvFromUnit(uv.y, kSkyViewSize.y));
}

// 視線方向dir(ワールド、Yが上)の、光源1つぶんの空の放射輝度(lut×radianceScale)。lut はその光源の
// Sky-View LUT(太陽用の gSkyViewLut、月用は呼び出し側が持つ)、lightDir は光源の方向。
// 空パスの背景と、IBLの環境ソースの焼き込みが共用する(空の色の定義を1か所にする)。太陽の円盤・夜の青は含まない。
// clampBelowHorizon=true: 地平線より下は、地平線のすぐ上の空の色にする(背景用。地形の外側に灰色の帯を出さない)。
// false: 地面に当たる向きは、LUTの地面の行(地面の反射込みの値)を引く(環境ソース用)。
// hitsGround: 視線が地面に当たる向きかどうか(clampBelowHorizonに関係なく同じ値)。
float3 SkyRadianceFromLut(Texture2D<float4> lut, float3 dir, float3 lightDir, float radianceScale,
                          bool clampBelowHorizon, out bool hitsGround) {
    float viewHeight = kBottomRadius + kObserverHeight;
    float horizonCos = sqrt(max(0.0, viewHeight * viewHeight - kBottomRadius * kBottomRadius)) / viewHeight;
    hitsGround = dir.y < -horizonCos;

    // 視線と光源の、水平面に射影した方位差のcos。真上・真下を向いているときは任意(1)。
    float3 sunHorizontal = float3(lightDir.x, 0.0, lightDir.z);
    float3 viewHorizontal = float3(dir.x, 0.0, dir.z);
    float sunLength = length(sunHorizontal);
    float viewLength = length(viewHorizontal);
    float lightViewCos = (sunLength > 1e-4 && viewLength > 1e-4)
        ? dot(sunHorizontal, viewHorizontal) / (sunLength * viewLength)
        : 1.0;

    bool useGroundRows = hitsGround && !clampBelowHorizon;
    float viewY = (hitsGround && clampBelowHorizon) ? -horizonCos : dir.y;
    float2 skyUv = SkyViewParamsToUv(useGroundRows, viewY, lightViewCos, viewHeight);
    if (!useGroundRows) {
        // 空側の最後の行(縦座標0.5の手前)に止める。境目を引くと、バイリニア補間で地面側の行が混ざる。
        skyUv.y = min(skyUv.y, 0.5 - 1.5 / kSkyViewSize.y);
    }
    return lut.SampleLevel(gLinearClamp, skyUv, 0).rgb * radianceScale;
}

// 太陽だけの空の放射輝度(gSkyViewLut)。
float3 SkyRadiance(float3 dir, float3 sunDir, float radianceScale, bool clampBelowHorizon,
                   out bool hitsGround) {
    return SkyRadianceFromLut(gSkyViewLut, dir, sunDir, radianceScale, clampBelowHorizon, hitsGround);
}

// 太陽 + 月明かりの空の放射輝度 + 星明かり(月のない夜空の一様な放射輝度。白)。moonLut は「月が太陽と同じ放射照度だったとき」の
// Sky-View LUT で、moonRatio(太陽の放射照度に対する月の放射照度の比)を掛けて足す。moonRatio が0なら月のLUTを引かない
// (中身が未生成でもよい)。星明かりは、地面に当たる向きには足さない(地面の反射はLUTの地面の行が持つ)。ただし
// clampBelowHorizon=true の地平線より下は地平線すれすれの空の色にしているので、星明かりも同じく足す。
// 空パスの背景・環境ソースの焼き込み・大気の空だけの平均が共用する。
float3 SkyRadianceWithMoon(Texture2D<float4> moonLut, float3 dir, float3 sunDir, float3 moonDir, float moonRatio,
                           float radianceScale, bool clampBelowHorizon, out bool hitsGround) {
    float3 sky = SkyRadiance(dir, sunDir, radianceScale, clampBelowHorizon, hitsGround);
    if (!hitsGround || clampBelowHorizon) sky += kNightSkyBaseRadianceRatio * radianceScale;
    if (moonRatio > 0.0) {
        bool moonHitsGround;
        sky += SkyRadianceFromLut(moonLut, dir, moonDir, radianceScale, clampBelowHorizon, moonHitsGround) * moonRatio;
    }
    return sky;
}

// ---- 媒質 ----
struct MediumSample {
    float3 scatteringRay;
    float3 scatteringMie;
    float3 scattering;
    float3 extinction;
};

MediumSample SampleMedium(float3 pos) {
    float h = max(0.0, length(pos) - kBottomRadius);
    float rayleighDensity = exp(-h / kRayleighScaleHeight);
    float mieDensity = exp(-h / kMieScaleHeight);
    float ozoneDensity = max(0.0, 1.0 - abs(h - kOzoneCenterHeight) / kOzoneHalfWidth);
    MediumSample m;
    m.scatteringRay = kRayleighScattering * rayleighDensity;
    m.scatteringMie = kMieScattering * mieDensity;
    m.scattering = m.scatteringRay + m.scatteringMie;
    float3 mieExtinction = (kMieScattering + kMieAbsorption) * mieDensity;
    m.extinction = m.scatteringRay + mieExtinction + kOzoneAbsorption * ozoneDensity;
    return m;
}

float RayleighPhase(float cosTheta) { return 3.0 / (16.0 * kPi) * (1.0 + cosTheta * cosTheta); }

float HenyeyGreensteinPhase(float g, float cosTheta) {
    float numer = 1.0 - g * g;
    float denom = 1.0 + g * g + 2.0 * g * cosTheta;
    return numer / (4.0 * kPi * denom * sqrt(denom));
}

// 視線に沿った光学的厚み(透過率LUTの生成用)。
float3 IntegrateOpticalDepth(float3 pos, float3 dir) {
    float tMax = RaySphereNearest(pos, dir, kTopRadius);
    float3 opticalDepth = 0.0;
    float t = 0.0;
    [loop]
    for (int i = 0; i < kTransmittanceSteps; ++i) {
        float newT = tMax * (float(i) + 0.3) / float(kTransmittanceSteps);
        float dt = newT - t;
        t = newT;
        opticalDepth += SampleMedium(pos + t * dir).extinction * dt;
    }
    return opticalDepth;
}

#if RS_USE_MULTISCAT
// 多重散乱LUTを引く。
float3 GetMultipleScattering(float3 pos, float sunCosZenith) {
    float2 uv = saturate(float2(sunCosZenith * 0.5 + 0.5,
                                (length(pos) - kBottomRadius) / (kTopRadius - kBottomRadius)));
    uv = float2(SubUvFromUnit(uv.x, kMultiScatSize), SubUvFromUnit(uv.y, kMultiScatSize));
    return gMultiScatLut.SampleLevel(gLinearClamp, uv, 0).rgb;
}
#endif

// 視線に沿った一次散乱の放射輝度。multiScatAs1 には「単位照度が多重散乱へ回る割合」を返す。
// globalL は太陽の照度(多重散乱LUT自身の生成では1)。
// 積分は、大気の上端か地面か maxDistance(km)のうち、手前で打ち切る。打ち切った点までの透過率を
// throughputOut に返す(大気の上端か地面まで積分したときは、そこまでの透過率)。
float3 IntegrateScatteredLuminanceUntil(float3 worldPos, float3 worldDir, float3 sunDir, float3 globalL,
                                        bool ground, float sampleCountIni, bool variableSampleCount,
                                        bool mieRayPhase, float maxDistance, out float3 multiScatAs1,
                                        out float3 throughputOut) {
    multiScatAs1 = 0.0;
    throughputOut = 1.0;
    float3 L = 0.0;
    float tBottom = RaySphereNearest(worldPos, worldDir, kBottomRadius);
    float tTop = RaySphereNearest(worldPos, worldDir, kTopRadius);
    float tMax = 0.0;
    if (tBottom < 0.0) {
        if (tTop < 0.0) return L;
        tMax = tTop;
    } else {
        tMax = (tTop > 0.0) ? min(tTop, tBottom) : tBottom;
    }
    tMax = min(tMax, maxDistance);

    float sampleCount = sampleCountIni;
    float sampleCountFloor = sampleCountIni;
    float tMaxFloor = tMax;
    if (variableSampleCount) {
        sampleCount = lerp(kSkyViewMinSteps, kSkyViewMaxSteps, saturate(tMax * 0.01));
        sampleCountFloor = floor(sampleCount);
        tMaxFloor = tMax * sampleCountFloor / sampleCount;
    }
    float dt = tMax / sampleCount;

    const float uniformPhase = 1.0 / (4.0 * kPi);
    float cosTheta = dot(sunDir, worldDir);
    float miePhaseValue = HenyeyGreensteinPhase(kMiePhaseG, -cosTheta);
    float rayleighPhaseValue = RayleighPhase(cosTheta);

    float3 throughput = 1.0;
    float t = 0.0;
    const float sampleSegmentT = 0.3;
    [loop]
    for (float s = 0.0; s < sampleCount; s += 1.0) {
        if (variableSampleCount) {
            // 手前に密なサンプル配置(区間ごとに2乗で伸ばす)。
            float t0 = s / sampleCountFloor;
            float t1 = (s + 1.0) / sampleCountFloor;
            t0 = t0 * t0;
            t1 = t1 * t1;
            t0 = tMaxFloor * t0;
            t1 = (t1 > 1.0) ? tMax : tMaxFloor * t1;
            t = t0 + (t1 - t0) * sampleSegmentT;
            dt = t1 - t0;
        } else {
            float newT = tMax * (s + sampleSegmentT) / sampleCount;
            dt = newT - t;
            t = newT;
        }

        float3 pos = worldPos + t * worldDir;
        MediumSample medium = SampleMedium(pos);
        float3 sampleTransmittance = exp(-medium.extinction * dt);

        float3 upVector = pos / length(pos);
        float3 sunTransmittance = TransmittanceToSun(pos, sunDir);

        float3 phaseTimesScattering = mieRayPhase
            ? medium.scatteringMie * miePhaseValue + medium.scatteringRay * rayleighPhaseValue
            : medium.scattering * uniformPhase;

        float tEarth = RaySphereNearest(pos + kPlanetRadiusOffset * upVector, sunDir, kBottomRadius);
        float earthShadow = tEarth >= 0.0 ? 0.0 : 1.0;

        float3 multiScatteredLuminance = 0.0;
#if RS_USE_MULTISCAT
        multiScatteredLuminance = GetMultipleScattering(pos, dot(sunDir, upVector));
#endif

        float3 S = globalL * (earthShadow * sunTransmittance * phaseTimesScattering +
                              multiScatteredLuminance * medium.scattering);

        // 区間内で解析的に積分する(減衰しながら足す)。
        float3 msIntegral = (medium.scattering - medium.scattering * sampleTransmittance) / medium.extinction;
        multiScatAs1 += throughput * msIntegral;
        L += throughput * (S - S * sampleTransmittance) / medium.extinction;
        throughput *= sampleTransmittance;
    }

    if (ground && tMax == tBottom && tBottom > 0.0) {
        // 地面での拡散反射(地面アルベド×太陽の直達×透過率)。
        float3 pos = worldPos + tBottom * worldDir;
        float3 upVector = pos / length(pos);
        float nDotL = saturate(dot(upVector, sunDir));
        L += globalL * TransmittanceToSun(pos, sunDir) * throughput * nDotL * kGroundAlbedo / kPi;
    }
    throughputOut = throughput;
    return L;
}

// 打ち切りなし(大気の上端か地面まで)の積分。透過率LUT・多重散乱LUT・Sky-View LUTの生成が使う。
float3 IntegrateScatteredLuminance(float3 worldPos, float3 worldDir, float3 sunDir, float3 globalL,
                                   bool ground, float sampleCountIni, bool variableSampleCount,
                                   bool mieRayPhase, out float3 multiScatAs1) {
    float3 throughputOut;
    return IntegrateScatteredLuminanceUntil(worldPos, worldDir, sunDir, globalL, ground, sampleCountIni,
                                            variableSampleCount, mieRayPhase, 1.0e30, multiScatAs1,
                                            throughputOut);
}
)HLSL";

// 共通部全体(定数+関数)。
inline std::string BuildAtmosphereCommonShader() {
    return BuildAtmosphereConstantsShader() + kTransmittanceUvShader + kAtmosphereFunctionsShader;
}

// LUT生成の3つのピクセルシェーダ(共通部の後ろへ連結して使う)。
// 透過率・多重散乱は引数の定数なし。Sky-Viewはルート定数1個(太陽方向のY成分=天頂とのcos)。
inline constexpr const char* kAtmosphereLutPassesShader = R"HLSL(
cbuffer SkyViewConstants : register(b0) { float gSunCosZenith; float gObserverHeightKm; };

float4 TransmittancePS(VSOut i) : SV_TARGET {
    float2 uv = i.pos.xy / kTransmittanceSize;
    float viewHeight, viewZenithCos;
    UvToTransmittanceParams(uv, viewHeight, viewZenithCos);
    float3 pos = float3(0.0, viewHeight, 0.0);
    float3 dir = float3(sqrt(saturate(1.0 - viewZenithCos * viewZenithCos)), viewZenithCos, 0.0);
    return float4(exp(-IntegrateOpticalDepth(pos, dir)), 1.0);
}

// 多重散乱LUT(横=太陽の天頂cos、縦=高度)。全球64方向の一次散乱の平均を二次散乱の光とし、
// 多重散乱で回る割合rの等比級数 1/(1-r) を掛ける(Hillaire 2020)。
float4 MultiScatPS(VSOut i) : SV_TARGET {
    float2 uv = i.pos.xy / kMultiScatSize;
    uv = float2(UnitFromSubUv(uv.x, kMultiScatSize), UnitFromSubUv(uv.y, kMultiScatSize));
    float sunCosZenith = uv.x * 2.0 - 1.0;
    float3 sunDir = float3(sqrt(saturate(1.0 - sunCosZenith * sunCosZenith)), sunCosZenith, 0.0);
    float viewHeight = kBottomRadius +
        saturate(uv.y + kPlanetRadiusOffset) * (kTopRadius - kBottomRadius - kPlanetRadiusOffset);
    float3 pos = float3(0.0, viewHeight, 0.0);

    float3 luminanceSum = 0.0;
    float3 ratioSum = 0.0;
    [loop]
    for (int a = 0; a < kMultiScatSqrtSamples; ++a) {
        [loop]
        for (int b = 0; b < kMultiScatSqrtSamples; ++b) {
            float randA = (float(a) + 0.5) / float(kMultiScatSqrtSamples);
            float randB = (float(b) + 0.5) / float(kMultiScatSqrtSamples);
            float theta = 2.0 * kPi * randA;
            float phi = acos(1.0 - 2.0 * randB); // 球面上の一様分布
            float3 dir = float3(cos(theta) * sin(phi), cos(phi), sin(theta) * sin(phi));
            float3 ratio;
            luminanceSum += IntegrateScatteredLuminance(pos, dir, sunDir, 1.0, true, kMultiScatSteps,
                                                        false, false, ratio);
            ratioSum += ratio;
        }
    }
    float sampleCount = float(kMultiScatSqrtSamples * kMultiScatSqrtSamples);
    // 球面全体の積分(各サンプル×4π/N)に等方位相(1/4π)を掛けると、サンプルの平均になる。
    float3 secondOrder = luminanceSum / sampleCount;
    float3 meanRatio = ratioSum / sampleCount;
    return float4(secondOrder / (1.0 - meanRatio), 1.0);
}

// Sky-View LUT。観測者は高さgObserverHeightKm、太陽は方位0の面内(Y成分だけ入力)。
// UVと視線の割り当て(地平線の角)は、引く側(SkyViewParamsToUv)と同じ基準の高さkObserverHeightで決め、
// 光を積分する位置だけが観測者の高さになる。視線が地面に当たる向きは、地面の反射込みの値になる。太陽の円盤は含まない。
float4 SkyViewPS(VSOut i) : SV_TARGET {
    float2 uv = i.pos.xy / kSkyViewSize;
    float mappingHeight = kBottomRadius + kObserverHeight;
    float viewZenithCos, lightViewCos;
    UvToSkyViewParams(uv, mappingHeight, viewZenithCos, lightViewCos);

    float3 sunDir = float3(sqrt(saturate(1.0 - gSunCosZenith * gSunCosZenith)), gSunCosZenith, 0.0);
    float3 pos = float3(0.0, kBottomRadius + gObserverHeightKm, 0.0);
    float viewZenithSin = sqrt(saturate(1.0 - viewZenithCos * viewZenithCos));
    float3 dir = float3(viewZenithSin * lightViewCos, viewZenithCos,
                        viewZenithSin * sqrt(saturate(1.0 - lightViewCos * lightViewCos)));

    float3 ratio;
    float3 L = IntegrateScatteredLuminance(pos, dir, sunDir, kSunIrradiance, true, kSkyViewMaxSteps,
                                           true, true, ratio);
    return float4(L, 1.0);
}
)HLSL";

} // namespace rs::render
