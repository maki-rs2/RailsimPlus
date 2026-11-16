//
// IblShader.h - IBL生成(環境ソースの焼き込み・拡散/鏡面の畳み込み・BRDF LUT)の HLSL シェーダ文字列
//
// 3種類の組み立てがある(使うレジスタが違うため、別々のソース文字列にする)。
//   BuildIblEnvBakeShader : 大気の共通部(t0..t2=大気LUT、t3=標準の空の遠景キューブ、t4=Envの鏡面キューブ、
//                           t5=大気の空だけの上半球平均、t6=月用のSky-View LUT)+環境ソースの焼き込み(EnvBakePS)・上半球平均(SkyMeanPS)・
//                           暗順応の倍率(AdaptationPS)
//   BuildIblFilterShader  : 上に加えて t3=環境ソースキューブ。環境ソースのミップ作成(DownsamplePS)・拡散(DiffusePS)・鏡面(SpecularPS)
//   BuildIblBrdfShader    : BRDF LUT(BrdfLutPS)。資源は引かない
// どの描画も AtmosphereShader.h の VSMain(全画面三角形)で描き、面の画素の位置は VSOut.ndc から求める
// (ndc.x=右が+、ndc.y=上が+。面の画素 (tx,ty)、tyは下向き、は u=2tx-1=ndc.x, v=2ty-1=-ndc.y)。
// 面の向きは AtmosphereMath.cpp の CubeFaceDirection と同じ式(変えるときは両方)。
// 座標はY-up。キューブの面の並びは +X,-X,+Y,-Y,+Z,-Z(D3D標準)。
//
#pragma once

#include <format>
#include <string>

#include "AtmosphereShader.h"
#include "GgxShader.h"
#include "IblParams.h"
#include "LandscapeShader.h"
#include "SunColorShader.h"
#include "../pipeline/ShaderColor.h"

namespace rs::render {

// サンプル数・BRDF LUTの大きさをHLSLの定数へ展開した文字列(IblParams.hから。CPUとの二重定義を避ける)。
inline std::string BuildIblConstantsShader() {
    namespace p = rs::render::ibl;
    return std::format(
        "static const uint kIblDiffuseSamples = {};\n"
        "static const uint kIblSpecularSamples = {};\n"
        "static const uint kIblBrdfSamples = {};\n"
        "static const float kIblEnvSourceSize = {}.0;\n"
        "static const uint kIblSkyMeanSamples = {};\n"
        "static const float kIblPhotoSunMaskDeg = {:.4f};\n"
        "static const float kIblPhotoLuminanceMin = {:.6f};\n",
        p::kIblDiffuseSampleCount, p::kIblSpecularSampleCount, p::kIblBrdfSampleCount, p::kIblEnvSourceSize,
        p::kIblSkyMeanSampleCount, p::kIblPhotoSunMaskDeg, p::kIblPhotoLuminanceMin);
}

// 資源を引かない部品(低食い違い列・面の向き・GGXの重点サンプリング)。
inline constexpr const char* kIblFunctionsShader = R"HLSL(
float RadicalInverse(uint bits) { return float(reversebits(bits)) * 2.3283064365386963e-10; }
float2 Hammersley(uint i, uint n) { return float2(float(i) / float(n), RadicalInverse(i)); }

// 面 face の画素(u,v ∈ [-1,1]、vは下向き)の方向。+X=(1,-v,-u)、-X=(-1,-v,u)、+Y=(u,1,v)、
// -Y=(u,-1,-v)、+Z=(u,-v,1)、-Z=(-u,-v,-1)。
float3 CubeFaceDirection(uint face, float u, float v) {
    float3 d;
    if (face == 0) d = float3(1.0, -v, -u);
    else if (face == 1) d = float3(-1.0, -v, u);
    else if (face == 2) d = float3(u, 1.0, v);
    else if (face == 3) d = float3(u, -1.0, -v);
    else if (face == 4) d = float3(u, -v, 1.0);
    else d = float3(-u, -v, -1.0);
    return normalize(d);
}

void BuildBasis(float3 n, out float3 t, out float3 b) {
    float3 up = abs(n.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

// GGX(alpha = roughness^2)の法線分布に従うハーフベクトル(ワールド)。
float3 ImportanceSampleGgx(float2 xi, float roughness, float3 n, float3 t, float3 b) {
    float a = roughness * roughness;
    float phi = 2.0 * kPi * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    return t * (sinTheta * cos(phi)) + b * (sinTheta * sin(phi)) + n * cosTheta;
}
)HLSL";

// 環境ソースの焼き込み。空の色は SkyRadiance(背景と同じ定義)をそのまま引く。clampBelowHorizon=false
// なので、地面に当たる向きはLUTの地面の行(地面の反射込み)になる。太陽の円盤・夜の青は入れない。
// 標準の空では、地平線より上の空へ写真の空の層(gUsePhoto が1のとき、Envの鏡面キューブのミップ0)を入れ、
// そのうえに SampleLandscape で遠景を重ねる(gUseLandscape が0なら重ねない)。背景(SkyShader.h)には
// 写真も遠景も入れない。
// cbuffer は EnvBakeConstants(IblGenerator.cpp)と同じ並び。float3+floatの組を1レジスタに詰める。
inline constexpr const char* kIblEnvBakeConstantsShader = R"HLSL(
cbuffer EnvBakeConstants : register(b0) {
    uint gFace;
    float3 gSunToSun;
    float gRadianceScale;
    float gUseLandscape;
    float gLandscapeYawRad;
    float gLandscapeBrightness;
    float3 gLandscapeTint; float gLandscapeSaturation;
    float gLandscapeContrast; float gLandscapeContrastPivot; float gLandscapeContrastScale; float gLandscapePad;
    float3 gPhotoSkyMean; float gPhotoSunFound;
    float3 gPhotoSunDirection; float gUsePhoto;
    float gPhotoYawCos;
    float gPhotoYawSin;
    float gMoonRatio; float gMoonPad;   // 月の放射照度の比(0=月なし)。同じレジスタに詰める
    float3 gMoonToMoon; float gMoonPad2;
    float gAdaptStart;       // 暗順応(AdaptationPS)。RenderTuning の nightAdaptationStart / Exponent / MaxGain
    float gAdaptExponent;
    float gAdaptMaxGain;
    float gAdaptBoost;       // 深夜の上乗せ(AtmosphereMath.cpp の NightAdaptationBoost を太陽高度から求めた値)
    float gAdaptDayStart;    // 昼の曲線(AdaptationPS)。RenderTuning の dayAdaptationStart / Exponent
    float gAdaptDayExponent;
    float gAdaptPad0;
    float gAdaptPad1;
};

// 追加テーブル。どれも使わない呼び出しでは別のキューブが入っていて、引かれない。
// t3: 標準の空の遠景キューブ。t4: Envの鏡面キューブ(写真)。t5: 大気の空だけの上半球平均(1x1のキューブの+X面)。
TextureCube<float4> gLandscapeCube : register(t3);
TextureCube<float4> gPhotoCube : register(t4);
TextureCube<float4> gSkyMeanCube : register(t5);
// t6: 月用のSky-View LUT(月が太陽と同じ放射照度だったときの空)。gMoonRatio > 0 のときだけ引く。
Texture2D<float4> gMoonSkyViewLut : register(t6);
// t7: 暗順応の1x1(x=倍率。AdaptationPS が先に描いた値)。EnvBakePS だけが引く。
Texture2D<float4> gExposureTex : register(t7);
)HLSL";

// 写真の空の層。式は EnvironmentState.h の RotateYaw(C++側)と同じ。
inline constexpr const char* kIblPhotoSkyShader = R"HLSL(
// 実際の太陽の方位を写真の太陽の方位へ重ねるY軸回りの回転。
float3 RotateYaw(float3 v, float c, float s) {
    return float3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

// 方向 dir(地平線より上)の写真の空の放射輝度。写真のHDR値は単位がそろっていないので、
// 大気の空の上半球平均(gSkyMeanCube)と写真の上半球平均(gPhotoSkyMean)の輝度の比をスカラーで掛ける
// (色ごとに合わせると、写真が大気の青に塗り替わる)。写真の太陽の周りは写真の平均色へ置き換える。
float3 PhotoSkyRadiance(float3 dir) {
    float3 photoDir = RotateYaw(dir, gPhotoYawCos, gPhotoYawSin);
    float3 photo = gPhotoCube.SampleLevel(gLinearClamp, photoDir, 0).rgb;
    if (gPhotoSunFound > 0.5) {
        float angle = acos(clamp(dot(photoDir, normalize(gPhotoSunDirection)), -1.0, 1.0));
        float radius = kIblPhotoSunMaskDeg * kPi / 180.0;
        float mask = 1.0 - smoothstep(0.5 * radius, radius, angle);
        photo = lerp(photo, gPhotoSkyMean, mask);
    }
    float3 skyMean = gSkyMeanCube.SampleLevel(gLinearClamp, float3(1.0, 0.0, 0.0), 0).rgb;
    float scale = dot(skyMean, kRec709LuminanceWeights) /
                  max(dot(gPhotoSkyMean, kRec709LuminanceWeights), kIblPhotoLuminanceMin);
    return photo * scale;
}
)HLSL";

inline constexpr const char* kIblEnvBakePassShader = R"HLSL(
// 環境ソースの焼き込み。出力には暗順応の倍率(プリ露出)を掛ける。空(大気・星明かり)・写真・遠景・
// 地面のすべてに掛かる。拡散・鏡面の畳み込みはここから作るので、IBLは自動でプリ露出の値になる。
// 倍率は AdaptationPS が同じ生成の中で先に描いたもの(未生成の0は1として扱う)。
float4 EnvBakeUnexposedPS(VSOut i);

float4 EnvBakePS(VSOut i) : SV_TARGET {
    float gain = max(gExposureTex.Load(int3(0, 0, 0)).x, 1.0);
    float4 color = EnvBakeUnexposedPS(i);
    return float4(color.rgb * gain, 1.0);
}

float4 EnvBakeUnexposedPS(VSOut i) {
    float3 dir = CubeFaceDirection(gFace, i.ndc.x, -i.ndc.y);
    float3 sunDir = normalize(gSunToSun);
    bool hitsGround;
    float3 radiance = SkyRadianceWithMoon(gMoonSkyViewLut, dir, sunDir, normalize(gMoonToMoon), gMoonRatio,
                                          gRadianceScale, false, hitsGround);
    // gUsePhoto: 0=写真なし、1=地平線より上の空の層、2=全方向(既定。遠景と大気の地面を重ねない)。
    if (gUsePhoto > 1.5) return float4(PhotoSkyRadiance(dir), 1.0);
    if (gUsePhoto > 0.5 && !hitsGround) radiance = PhotoSkyRadiance(dir);
    float4 landscape = SampleLandscape(gLandscapeCube, dir, sunDir, gRadianceScale);
    return float4(CompositeLandscape(radiance, landscape), 1.0);
}

// 大気の空だけの上半球のコサイン重み付き平均の放射輝度(1画素)。写真の空の層の明るさを合わせる基準。
// 上半球は地平線より下へ行かない向き(clampBelowHorizon=true)で引く。太陽の円盤・遠景・地面は含めない。
// 面+Xの1x1へ描く(キューブの+X面を、方向(1,0,0)で引く)。
float4 SkyMeanPS(VSOut i) : SV_TARGET {
    float3 sunDir = normalize(gSunToSun);
    float3 sum = 0.0;
    [loop]
    for (uint s = 0; s < kIblSkyMeanSamples; ++s) {
        float2 xi = Hammersley(s, kIblSkyMeanSamples);
        float phi = 2.0 * kPi * xi.x;
        float cosTheta = sqrt(1.0 - xi.y);
        float sinTheta = sqrt(xi.y);
        float3 dir = float3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);
        bool hitsGround;
        sum += SkyRadianceWithMoon(gMoonSkyViewLut, dir, sunDir, normalize(gMoonToMoon), gMoonRatio,
                                   gRadianceScale, true, hitsGround);
    }
    return float4(sum / float(kIblSkyMeanSamples), 1.0);
}

// 暗順応の倍率(AtmosphereMath.cpp の NightAdaptationGain と同じ式。変えるときは両方)。
float AdaptationGain(float e, float start, float exponent, float maxGain) {
    if (e >= start) return 1.0;
    return clamp(pow(start / max(e, 1e-12), exponent), 1.0, max(maxGain, 1.0));
}

// 順応の倍率。夜の曲線と昼の曲線の大きいほう(AtmosphereMath.cpp の ExposureAdaptationGain と同じ合成。変えるときは両方)。
float ExposureAdaptationGain(float e) {
    return max(AdaptationGain(e, gAdaptStart, gAdaptExponent, gAdaptMaxGain),
               AdaptationGain(e, gAdaptDayStart, gAdaptDayExponent, gAdaptMaxGain));
}

// 暗順応の明るさ E と倍率(1x1へ描く。x=倍率、y=E)。SkyMeanPS の結果(t5。月明かり・星明かりを含む上半球平均)を読む。
// E = 空の上半球のコサイン重み付き平均の輝度(太陽の放射照度πに対する比の単位)。
float4 AdaptationPS(VSOut i) : SV_TARGET {
    // 順応の明るさは、見えている景色の大半を占める空の明るさ(上半球のコサイン重み付き平均の放射輝度。月明かりの空・
    // 星明かりを含む)で測る。直射光の水平面照度は入れない(満月の夜は照度の大半が月の直射光だが、画面の空は星明かりの
    // 約20倍しか明るくないので、照度で合わせると月夜の空が暗く、月のない夜の空が明るく持ち上がって逆転する)。
    float3 skyMean = gSkyMeanCube.SampleLevel(gLinearClamp, float3(1.0, 0.0, 0.0), 0).rgb;
    float e = dot(skyMean, kRec709LuminanceWeights);
    return float4(ExposureAdaptationGain(e) * max(gAdaptBoost, 1.0), e, 0.0, 1.0);
}
)HLSL";

// 環境ソースのミップ作成と、拡散・鏡面の畳み込み。入力は環境ソースキューブ(ミップ付き)。
// 畳み込みは filtered importance sampling: サンプルごとに、そのサンプルが代表する立体角
// (1 / (サンプル数 × pdf))と環境ソースの1テクセルの立体角を比べてミップを選ぶ。細かい模様を
// 少ないサンプルで拾ったときのざらつきを、粗いミップを引くことで抑える。
inline constexpr const char* kIblFilterPassesShader = R"HLSL(
TextureCube<float4> gEnvCube : register(t3);

cbuffer FilterConstants : register(b0) {
    uint gFace;
    float gRoughness;
    float gSrcMip;   // DownsamplePS が読むミップ(作るミップの1つ上)
};

float3 EnvRadiance(float3 dir, float lod) { return gEnvCube.SampleLevel(gLinearClamp, dir, lod).rgb; }

// 環境ソースの1テクセルの立体角(キューブの6面で球全体 4π を等分した平均)。
static const float kEnvTexelSolidAngle = 4.0 * kPi / (6.0 * kIblEnvSourceSize * kIblEnvSourceSize);
// サンプルの立体角が1テクセルと等しいときのミップに足す補正(filtered importance sampling の式の +1)。
static const float kEnvLodBias = 1.0;

// サンプル密度 pdf(立体角あたり)のサンプルが代表する立体角から、引くミップを選ぶ。
float EnvSampleLod(float pdf, uint sampleCount) {
    float sampleSolidAngle = 1.0 / (float(sampleCount) * max(pdf, 1e-6));
    return max(0.0, 0.5 * log2(sampleSolidAngle / kEnvTexelSolidAngle) + kEnvLodBias);
}

// 環境ソースのミップ作成: 作るミップの画素の方向で、1つ上のミップを引く。画素の中心は上のミップの
// 4テクセルの角に当たるので、バイリニアで2x2の平均になる(キューブの面の境目も面をまたいで補間される)。
float4 DownsamplePS(VSOut i) : SV_TARGET {
    float3 dir = CubeFaceDirection(gFace, i.ndc.x, -i.ndc.y);
    return float4(EnvRadiance(dir, gSrcMip), 1.0);
}

// 拡散: cosine重み付き半球サンプリングの放射輝度の平均(=放射照度÷π)。pdf = cosθ/π。
float4 DiffusePS(VSOut i) : SV_TARGET {
    float3 n = CubeFaceDirection(gFace, i.ndc.x, -i.ndc.y);
    float3 t, b;
    BuildBasis(n, t, b);
    float3 sum = 0.0;
    [loop]
    for (uint s = 0; s < kIblDiffuseSamples; ++s) {
        float2 xi = Hammersley(s, kIblDiffuseSamples);
        float phi = 2.0 * kPi * xi.x;
        float cosTheta = sqrt(1.0 - xi.y);
        float sinTheta = sqrt(xi.y);
        float3 l = t * (cos(phi) * sinTheta) + b * (sin(phi) * sinTheta) + n * cosTheta;
        sum += EnvRadiance(l, EnvSampleLod(cosTheta / kPi, kIblDiffuseSamples));
    }
    return float4(sum / float(kIblDiffuseSamples), 1.0);
}

// 鏡面: N=V=R として GGX 重点サンプリング。roughness 0 は環境ソースそのまま(ミップ0)。
// pdf = D(ndh) × ndh / (4 vdh)。N=V なので ndh = vdh で、D / 4。
float4 SpecularPS(VSOut i) : SV_TARGET {
    float3 n = CubeFaceDirection(gFace, i.ndc.x, -i.ndc.y);
    if (gRoughness <= 0.0) return float4(EnvRadiance(n, 0.0), 1.0);
    float3 t, b;
    BuildBasis(n, t, b);
    float alpha = gRoughness * gRoughness;
    float3 sum = 0.0;
    float weight = 0.0;
    [loop]
    for (uint s = 0; s < kIblSpecularSamples; ++s) {
        float2 xi = Hammersley(s, kIblSpecularSamples);
        float3 h = ImportanceSampleGgx(xi, gRoughness, n, t, b);
        float3 l = normalize(2.0 * dot(n, h) * h - n);
        float nDotL = saturate(dot(n, l));
        if (nDotL > 0.0) {
            float pdf = DistributionGgxAlpha(saturate(dot(n, h)), alpha) * 0.25;
            sum += EnvRadiance(l, EnvSampleLod(pdf, kIblSpecularSamples)) * nDotL;
            weight += nDotL;
        }
    }
    return float4(sum / max(weight, 1e-4), 1.0);
}
)HLSL";

// BRDF LUT(split-sum)。横=NdotV、縦=roughness(上が0)。出力RG = (A, B): 鏡面 = F0*A + B。
inline constexpr const char* kIblBrdfPassShader = R"HLSL(
float2 IntegrateBrdf(float nDotV, float roughness) {
    float3 v = float3(sqrt(max(0.0, 1.0 - nDotV * nDotV)), 0.0, nDotV);
    float3 n = float3(0.0, 0.0, 1.0);
    float k = roughness * roughness * 0.5; // IBL用のSmithのk
    float a = 0.0;
    float b = 0.0;
    [loop]
    for (uint s = 0; s < kIblBrdfSamples; ++s) {
        float2 xi = Hammersley(s, kIblBrdfSamples);
        float3 h = ImportanceSampleGgx(xi, roughness, n, float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0));
        float3 l = 2.0 * dot(v, h) * h - v;
        float nDotL = saturate(l.z);
        float nDotH = saturate(h.z);
        float vDotH = saturate(dot(v, h));
        if (nDotL > 0.0) {
            float gl = nDotL / (nDotL * (1.0 - k) + k);
            float gv = nDotV / (nDotV * (1.0 - k) + k);
            float gVis = (gl * gv) * vDotH / max(nDotH * nDotV, 1e-5);
            float fc = pow(1.0 - vDotH, 5.0);
            a += (1.0 - fc) * gVis;
            b += fc * gVis;
        }
    }
    return float2(a, b) / float(kIblBrdfSamples);
}

float4 BrdfLutPS(VSOut i) : SV_TARGET {
    float nDotV = max(i.ndc.x * 0.5 + 0.5, 1e-3);
    float roughness = max(-i.ndc.y * 0.5 + 0.5, 1e-3);
    return float4(IntegrateBrdf(nDotV, roughness), 0.0, 1.0);
}
)HLSL";

inline std::string BuildIblEnvBakeShader() {
    return std::string(kColorConstantsShader) + BuildAtmosphereCommonShader() + BuildIblConstantsShader() +
           kIblFunctionsShader + kSunColorShader +
           kIblEnvBakeConstantsShader + kLandscapeFunctionsShader + kIblPhotoSkyShader + kIblEnvBakePassShader;
}

inline std::string BuildIblFilterShader() {
    return BuildAtmosphereCommonShader() + BuildIblConstantsShader() + kIblFunctionsShader + kGgxDistributionShader +
           kIblFilterPassesShader;
}

inline std::string BuildIblBrdfShader() {
    return BuildAtmosphereCommonShader() + BuildIblConstantsShader() + kIblFunctionsShader +
           kIblBrdfPassShader;
}

} // namespace rs::render
