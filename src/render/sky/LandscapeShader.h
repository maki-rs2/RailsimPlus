//
// LandscapeShader.h - 標準の空の組み込み遠景の HLSL シェーダ文字列
//
// kLandscapeFunctionsShader: 環境ソースの焼き込み(IblShader.h の EnvBakePS)が、遠景を引いて光を掛け、
// 空へ合成する。背景(SkyShader.h)には遠景を合成しない。
// 連結の前提: 大気の共通部(AtmosphereShader.h。SkyRadiance・gTransmittanceLut・gLinearClamp)・
// SunColorShader.h・呼び出し側の cbuffer が次の変数を持つこと(EnvBakeConstants と同名)。
//   float3 gLandscapeTint; float gLandscapeSaturation; float gUseLandscape(1で有効); float gLandscapeYawRad;
//   float gLandscapeBrightness; float gLandscapeContrast; float gLandscapeContrastPivot; float gLandscapeContrastScale;
// 遠景キューブのレジスタは呼び出し側が宣言する(焼き込み=t3)ので、ここでは宣言せず、
// SampleLandscape の引数で受ける。
//
// kLandscapeBakeMeshShader・kLandscapeBakeDownsampleShader: 遠景の画像を6面のキューブへ焼く描画
// (LandscapeCube)のシェーダ。別々にコンパイルする(t0・b0を別の用途で使うため)。
// キューブの値は「アルベド×カバレッジ(プリマルチプライ)、カバレッジ」。光は掛けない。
//
#pragma once

#include <string>

namespace rs::render {

inline constexpr const char* kLandscapeFunctionsShader = R"HLSL(
// 空の平均輝度(遠景を照らす空からの光)。Sky-View LUTを固定の方向で引いた、余弦重み付きの平均。
// 方位8×仰角3段(15,45,75度)+天頂。各方向の重みは sin(el)*cos(el)(水平な面の余弦×立体角)。
// 天頂の重みは、仰角75度の1方向と同じにする。拡散キューブは遠景を含むので引かない(循環するため)。
static const float kLandscapeRingElevation[3] = {0.2617994, 0.7853982, 1.3089969}; // 15,45,75度[rad]

float3 LandscapeAverageSkyRadiance(float3 sunDir, float radianceScale) {
    float3 sum = 0.0;
    float weightSum = 0.0;
    bool hitsGround;
    [unroll]
    for (int ring = 0; ring < 3; ++ring) {
        float el = kLandscapeRingElevation[ring];
        float weight = sin(el) * cos(el);
        [unroll]
        for (int a = 0; a < 8; ++a) {
            float az = float(a) * (2.0 * kPi / 8.0);
            float3 dir = float3(sin(az) * cos(el), sin(el), cos(az) * cos(el));
            sum += SkyRadiance(dir, sunDir, radianceScale, true, hitsGround) * weight;
            weightSum += weight;
        }
    }
    float zenithWeight = sin(kLandscapeRingElevation[2]) * cos(kLandscapeRingElevation[2]);
    sum += SkyRadiance(float3(0.0, 1.0, 0.0), sunDir, radianceScale, true, hitsGround) * zenithWeight;
    weightSum += zenithWeight;
    return sum / weightSum;
}

// アルベドの明暗の幅を広げる。式は LandscapeContrast.cpp の ApplyLandscapeContrast と同じ(変えるときは両方)。
// 輝度を、画像の輝度の幾何平均(gLandscapeContrastPivot)を中心に対数の上で gLandscapeContrast 倍にし、色の比は保つ。
// 平均の明るさを保つ倍率(gLandscapeContrastScale、画像を読んだときにCPUで求める)を掛け、
// 反射率は1を超えられないので、最大のチャンネルを1で止める。
static const float kLandscapeLuminanceMin = 1e-4;

float3 ApplyLandscapeContrast(float3 albedo) {
    float luma = max(dot(albedo, kRec709LuminanceWeights), kLandscapeLuminanceMin);
    float pivot = max(gLandscapeContrastPivot, kLandscapeLuminanceMin);
    float contrasted = pivot * pow(luma / pivot, gLandscapeContrast);
    float3 result = albedo * (contrasted / luma * gLandscapeContrastScale);
    float peak = max(result.r, max(result.g, result.b));
    return peak > 1.0 ? result / peak : result;
}

// 遠景の面の向き。山並みの斜面と木立ちの面は、急な面と平らな面が混ざるので、見ている側(原点)を向いて
// 45度上を向いた面で代表させる(水平な地面として扱うと、どの方位でも真上から日が当たり、明るく平たくなる)。
static const float kLandscapeSurfaceElevationRad = 0.7853982; // 45度
static const float kLandscapeHorizontalMin = 1e-4;

float3 LandscapeSurfaceNormal(float3 dir) {
    float2 horizontal = dir.xz;
    float len = length(horizontal);
    if (len < kLandscapeHorizontalMin) return float3(0.0, 1.0, 0.0);
    float2 towardViewer = -horizontal / len;
    float c = cos(kLandscapeSurfaceElevationRad);
    float s = sin(kLandscapeSurfaceElevationRad);
    return float3(towardViewer.x * c, s, towardViewer.y * c);
}

// 方向 dir の遠景。戻り値 = (放射輝度RGB(カバレッジを掛け済み)、カバレッジ)。
// 放射輝度 = アルベド' × (太陽色 × saturate(面の法線・太陽方向) + 空の平均輝度 × 面から見える空の割合)。
// 面は LandscapeSurfaceNormal(見ている側を向いた斜面)。太陽の側の面は逆光で暗く、反対側の面は明るい。
// 面から見える空の割合は、傾いた面の天空率 (1 + n.y) / 2。
// アルベド' = lerp(輝度, アルベド, 彩度) × 色の倍率 × 明るさ(どれも線形なので、プリマルチプライのまま掛けてよい)。
// yawは、方位が増える向きへ遠景が回って見えるように、引く方向を逆に回す。
// gUseLandscape が0なら、キューブの中身に関係なく (0,0,0,0)(キューブの枠に別のキューブが入っていても引かない)。
// 勾配つきの Sample を使うので、分岐の中から呼ばないこと。
float4 SampleLandscape(TextureCube<float4> landscapeCube, float3 dir, float3 sunDir, float radianceScale) {
    float s, c;
    sincos(gLandscapeYawRad, s, c);
    float3 sampleDir = float3(c * dir.x - s * dir.z, dir.y, s * dir.x + c * dir.z);
    float4 texel = landscapeCube.Sample(gLinearClamp, sampleDir);
    if (gUseLandscape < 0.5 || texel.a <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);

    // キューブはプリマルチプライ(アルベド×カバレッジ)なので、カバレッジで割ってから明暗を広げ、掛け戻す。
    float3 contrasted = ApplyLandscapeContrast(texel.rgb / texel.a) * texel.a;
    float luma = dot(contrasted, kRec709LuminanceWeights);
    float3 albedo = lerp(luma.xxx, contrasted, gLandscapeSaturation) * gLandscapeTint * gLandscapeBrightness;
    float3 sunColor = SunLightFromTransmittance(gTransmittanceLut, gLinearClamp, sunDir);
    float3 normal = LandscapeSurfaceNormal(dir);
    float skyViewFactor = 0.5 * (1.0 + normal.y);
    float3 light = sunColor * saturate(dot(normal, sunDir)) +
                   LandscapeAverageSkyRadiance(sunDir, radianceScale) * skyViewFactor;
    return float4(albedo * light, texel.a);
}

// 空の上に遠景を重ねる(landscape はプリマルチプライ済み)。
float3 CompositeLandscape(float3 sky, float4 landscape) {
    return sky * (1.0 - landscape.a) + landscape.rgb;
}
)HLSL";

// 焼き込み(mip0)。メッシュは位置(原点からの単位方向)+UV。面のカメラ(右・上・前)から、行列なしで射影する
// (割り算はラスタライザに任せ、手前側と背面は w>=near のクリップで落とす)。
inline constexpr const char* kLandscapeBakeMeshShader = R"HLSL(

cbuffer FaceConstants : register(b0) {
    float3 gRight;   float gPad0;
    float3 gUp;      float gPad1;
    float3 gForward; float gPad2;
};

static const float kNear = 0.01;

struct MeshVSIn { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct MeshVSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

MeshVSOut MeshVS(MeshVSIn i) {
    float w = dot(i.pos, gForward);
    MeshVSOut o;
    o.pos = float4(dot(i.pos, gRight), dot(i.pos, gUp), w - kNear, w);
    o.uv = i.uv;
    return o;
}

Texture2D<float4> gImage : register(t0); // R8G8B8A8_UNORM_SRGB(読むとリニア)

// 画像の1画素(横は周回、縦はクランプ)。リニアのRGB×アルファ。
float4 LoadPremultiplied(int2 p, int2 size) {
    p.x = ((p.x % size.x) + size.x) % size.x;
    p.y = clamp(p.y, 0, size.y - 1);
    float4 c = gImage.Load(int3(p, 0));
    return float4(c.rgb * c.a, c.a);
}

// プリマルチプライしてから双線形補間する(透明な画素の色が縁ににじまない。横の縫い目もつながる)。
float4 MeshPS(MeshVSOut i) : SV_TARGET {
    float2 sizeF;
    gImage.GetDimensions(sizeF.x, sizeF.y);
    int2 size = int2(sizeF);
    float2 p = i.uv * sizeF - 0.5;
    int2 p0 = int2(floor(p));
    float2 f = p - float2(p0);
    float4 top = lerp(LoadPremultiplied(p0, size), LoadPremultiplied(p0 + int2(1, 0), size), f.x);
    float4 bottom = lerp(LoadPremultiplied(p0 + int2(0, 1), size), LoadPremultiplied(p0 + int2(1, 1), size), f.x);
    return lerp(top, bottom, f.y);
}

)HLSL";

// ミップ: 1つ上のミップの同じ面の2x2を平均する(箱フィルタ。プリマルチプライ済みなのでそのまま平均してよい)。
inline constexpr const char* kLandscapeBakeDownsampleShader = R"HLSL(
Texture2DArray<float4> gSource : register(t0); // 1つ上のミップ(1ミップだけを見るSRV)
cbuffer DownsampleConstants : register(b0) { uint gFace; };

struct FullVSOut { float4 pos : SV_POSITION; };

FullVSOut FullscreenVS(uint vid : SV_VertexID) {
    FullVSOut o;
    float2 p = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float4 DownsamplePS(FullVSOut i) : SV_TARGET {
    int2 base = int2(i.pos.xy) * 2;
    float4 sum = gSource.Load(int4(base, gFace, 0)) + gSource.Load(int4(base + int2(1, 0), gFace, 0)) +
                 gSource.Load(int4(base + int2(0, 1), gFace, 0)) + gSource.Load(int4(base + int2(1, 1), gFace, 0));
    return sum * 0.25;
}
)HLSL";

} // namespace rs::render
