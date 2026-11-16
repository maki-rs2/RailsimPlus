//
// MotionBlurShader.h - モーションブラーの HLSL(DXC、SM6.6。ResourceDescriptorHeap[] で SRV を番号から引く)
//
// パスは4つ。1) 速度の合成(カメラの動き+物体の速度、画素単位の半径ベクトルと線形の深度)
// 2) タイルごとの最大の速度(横→縦の2回) 3) 周囲 3x3 のタイルの最大 4) 速度の向きに拾って混ぜる。
// ぼかしの重みは、McGuire らの再構成フィルタ(A Reconstruction Filter for Plausible Motion Blur)。
//
#pragma once

#include <string>

namespace rs::render {

inline const std::string kMotionBlurShader = R"(
cbuffer MotionBlurConstants : register(b0) {
    row_major float4x4 gInvViewProj;   // 今フレームの(ジッタなしの)viewProj の逆行列
    row_major float4x4 gPrevViewProj;  // 前フレームの(ジッタなしの)viewProj
    float2 gInvSize;                   // 1 / 画面の大きさ
    float2 gSize;                      // 画面の大きさ
    float gShutter;                    // 1フレームの動きのうち、ぼかしの長さにする割合
    float gMaxRadius;                  // ぼかしの半径の上限[画素]
    uint gSamples;                     // 速度の向きに拾う点の数
    uint gFrame;                       // ノイズの位相(フレーム番号)
    uint gTileSize;                    // タイルの1辺[画素]
    uint gColorIdx;                    // 入力(シーンの HDR)の SRV 番号
    uint gDepthIdx;                    // 本編の深度(R32_FLOAT。Reversed-Z)の SRV 番号
    uint gObjectVelocityIdx;           // 物体の速度(RG16F。NDC の前 − 今)の SRV 番号
    uint gVelocityIdx;                 // 合成した速度(xy=半径ベクトル[画素]、z=線形の深度)の SRV 番号
    uint gPartialIdx;                  // タイルの途中結果(横だけ縮めた)の SRV 番号
    uint gTileIdx;                     // タイルごとの最大の速度の SRV 番号
    uint gNeighborIdx;                 // 周囲 3x3 のタイルの最大の速度の SRV 番号
};

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float2 PixelToNdc(float2 pixelCenter) {
    return float2(pixelCenter.x * gInvSize.x * 2.0 - 1.0, 1.0 - pixelCenter.y * gInvSize.y * 2.0);
}

// 異常値(NaN・無限大・負)は 0、極端に大きい値は fp16 の上限の手前で止める。
float3 Sanitize(float3 c) {
    c = select(isfinite(c), c, float3(0.0, 0.0, 0.0));
    return clamp(c, 0.0, 60000.0);
}

// 1) 速度の合成。カメラの動きは深度から世界座標を復元して前フレームの viewProj で射影し、物体の速度を足す。
// 「前 − 今」の動きを、シャッターの割合の半分(中心から前後へ広げる片側)で画素単位の半径ベクトルにして、上限で止める。
float4 PSVelocity(VSOut input) : SV_Target {
    Texture2D<float> depthTex = ResourceDescriptorHeap[gDepthIdx];
    Texture2D<float2> objectTex = ResourceDescriptorHeap[gObjectVelocityIdx];
    int2 pixel = int2(input.pos.xy);
    float depth = depthTex.Load(int3(pixel, 0));
    float2 ndc = PixelToNdc(float2(pixel) + 0.5);
    float4 world = mul(float4(ndc, depth, 1.0), gInvViewProj);
    // 逆行列を掛けた w は、クリップ空間の w(視線方向の距離)の逆数。
    float linearDepth = min(1.0 / max(abs(world.w), 1.0e-9), 60000.0);
    world.xyz /= world.w;
    float4 prevClip = mul(float4(world.xyz, 1.0), gPrevViewProj);
    float2 prevNdc = prevClip.w > 1.0e-5 ? prevClip.xy / prevClip.w : ndc;
    prevNdc += objectTex.Load(int3(pixel, 0));
    float2 delta = prevNdc - ndc;
    // NDC の y は上が正、画素の y は下が正。
    float2 radius = float2(delta.x * 0.5 * gSize.x, -delta.y * 0.5 * gSize.y) * (gShutter * 0.5);
    float len = length(radius);
    if (!isfinite(len)) radius = float2(0.0, 0.0);
    else if (len > gMaxRadius) radius *= gMaxRadius / len;
    return float4(radius, linearDepth, 0.0);
}

float2 LongerOf(float2 a, float2 b) { return dot(b, b) > dot(a, a) ? b : a; }

// 2) タイルごとの最大の速度。横(画面の横をタイル幅に縮めた RT へ)→ 縦(タイルの RT へ)の2回に分ける
// (長さの最大は、先に行ごとの最大を取ってから縦に取っても同じ)。
float2 PSTilePartial(VSOut input) : SV_Target {
    Texture2D<float4> velocityTex = ResourceDescriptorHeap[gVelocityIdx];
    int2 p = int2(input.pos.xy);
    int maxX = int(gSize.x) - 1;
    float2 best = float2(0.0, 0.0);
    for (uint i = 0; i < gTileSize; ++i) {
        int x = min(p.x * int(gTileSize) + int(i), maxX);
        best = LongerOf(best, velocityTex.Load(int3(x, p.y, 0)).xy);
    }
    return best;
}

float2 PSTileMax(VSOut input) : SV_Target {
    Texture2D<float2> partialTex = ResourceDescriptorHeap[gPartialIdx];
    int2 p = int2(input.pos.xy);
    int maxY = int(gSize.y) - 1;
    float2 best = float2(0.0, 0.0);
    for (uint i = 0; i < gTileSize; ++i) {
        int y = min(p.y * int(gTileSize) + int(i), maxY);
        best = LongerOf(best, partialTex.Load(int3(p.x, y, 0)));
    }
    return best;
}

// 3) 周囲 3x3 のタイルの最大。画素を通るぼかしは、自分のタイルと隣のタイルの速度のどれかの向きに伸びる。
float2 PSNeighborMax(VSOut input) : SV_Target {
    Texture2D<float2> tileTex = ResourceDescriptorHeap[gTileIdx];
    int2 p = int2(input.pos.xy);
    int2 maxTile = int2(ceil(gSize / float(gTileSize))) - 1;
    float2 best = float2(0.0, 0.0);
    [unroll] for (int dy = -1; dy <= 1; ++dy) {
        [unroll] for (int dx = -1; dx <= 1; ++dx) {
            int2 q = clamp(p + int2(dx, dy), int2(0, 0), maxTile);
            best = LongerOf(best, tileTex.Load(int3(q, 0)));
        }
    }
    return best;
}

float InterleavedGradientNoise(float2 pixel, uint frame) {
    pixel += 5.588238 * float(frame & 7u);
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// a が b より手前なら 1、奥へ離れるほど 0 へ。距離に比例した幅でなめらかにつなぐ。
float SoftDepthCompare(float a, float b) {
    float extent = max(0.25, 0.05 * min(a, b));
    return saturate(1.0 - (a - b) / extent);
}
float Cone(float dist, float len) { return saturate(1.0 - dist / max(len, 1.0e-4)); }
float Cylinder(float dist, float len) { return 1.0 - smoothstep(0.95 * len, 1.05 * len + 1.0e-4, dist); }

// 4) 速度の向きに拾って混ぜる。向きと拾う範囲は周囲のタイルの最大の速度、重みは各画素の速度と深度で決める
// (手前の物のぼかしが奥へ滲み、静止した手前の物は奥の動きでぼけない)。
float4 PSGather(VSOut input) : SV_Target {
    Texture2D<float4> colorTex = ResourceDescriptorHeap[gColorIdx];
    Texture2D<float4> velocityTex = ResourceDescriptorHeap[gVelocityIdx];
    Texture2D<float2> neighborTex = ResourceDescriptorHeap[gNeighborIdx];
    int2 pixel = int2(input.pos.xy);
    int2 maxPixel = int2(gSize) - 1;
    float3 center = Sanitize(colorTex.Load(int3(pixel, 0)).rgb);
    float2 neighborRadius = neighborTex.Load(int3(pixel / int(gTileSize), 0));
    float neighborLen = length(neighborRadius);
    if (neighborLen < 0.5) return float4(center, 1.0);

    float4 centerVelocity = velocityTex.Load(int3(pixel, 0));
    float lenX = length(centerVelocity.xy);
    float zX = centerVelocity.z;
    float2 dir = neighborRadius / neighborLen;
    float jitter = InterleavedGradientNoise(float2(pixel), gFrame) - 0.5;

    // 自分自身(距離 0)の重みは、速度が 0 でも最大(4)。
    float weight = 4.0;
    float3 sum = center * weight;
    float invSamples = 1.0 / float(gSamples);
    for (uint i = 0; i < gSamples; ++i) {
        float t = ((float(i) + 0.5 + jitter) * invSamples) * 2.0 - 1.0;
        int2 q = clamp(int2(floor(float2(pixel) + 0.5 + dir * (t * neighborLen))), int2(0, 0), maxPixel);
        float dist = length(float2(q - pixel));
        float4 sampleVelocity = velocityTex.Load(int3(q, 0));
        float lenY = length(sampleVelocity.xy);
        float zY = sampleVelocity.z;
        float alpha = SoftDepthCompare(zY, zX) * Cone(dist, lenY) + SoftDepthCompare(zX, zY) * Cone(dist, lenX) +
                      2.0 * Cylinder(dist, lenY) * Cylinder(dist, lenX);
        sum += Sanitize(colorTex.Load(int3(q, 0)).rgb) * alpha;
        weight += alpha;
    }
    return float4(sum / weight, 1.0);
}
)";

} // namespace rs::render
