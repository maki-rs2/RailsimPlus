//
// DepthOfFieldShader.h - フォトモードの被写界深度の HLSL(DXC、SM6.6。ResourceDescriptorHeap[] で SRV を番号から引く)
//
// パスは6段(2 のタイルの最大は横・縦の2回に分ける)。1) プリフィルタ(半解像度。色と符号付きのぼけの半径) 2) 近景のぼけの最大(タイル。横→縦の2回)
// 3) 周囲 3x3 のタイルの最大 4) 遠景のギャザー(半解像度) 5) 近景のギャザー(半解像度) 6) 合成(フル解像度)。
// ぼけの半径は、フル解像度の画素での半径(符号付き)で持つ。フォーカス距離より手前(近景)が負、奥(遠景)が正。
// ギャザーのカーネルは円盤(黄金角のらせん)で、重みの和でエネルギーを正規化する。
//
#pragma once

#include <string>

namespace rs::render {

inline const std::string kDepthOfFieldShader = R"(
cbuffer DepthOfFieldConstants : register(b0) {
    row_major float4x4 gInvViewProj;   // 今フレームの(ジッタなしの)viewProj の逆行列
    float2 gSize;                      // フル解像度の大きさ[画素]
    float2 gInvSize;                   // 1 / フル解像度の大きさ
    float2 gHalfSize;                  // 半解像度の大きさ[画素](切り上げ)
    float gFocalMm;                    // 焦点距離[mm]
    float gAperture;                   // 絞り(F値)
    float gFocusMm;                    // フォーカス距離[mm](最短撮影距離以上に丸めてある)
    float gMmToPx;                     // センサー上の 1mm が、出力の何画素にあたるか
    float gMaxRadius;                  // ぼけの半径の上限[フル解像度の画素]
    uint gSamples;                     // 円盤の中で拾う点の数
    uint gFrame;                       // ノイズの位相(フレーム番号)
    uint gTileSize;                    // タイルの1辺[半解像度の画素]
    uint gColorIdx;                    // 入力(シーンの HDR)の SRV 番号
    uint gDepthIdx;                    // 本編の深度(R32_FLOAT。Reversed-Z)の SRV 番号
    uint gPrefilterIdx;                // プリフィルタ(rgb=色、a=符号付きのぼけの半径)の SRV 番号
    uint gPartialIdx;                  // タイルの途中結果(横だけ縮めた)の SRV 番号
    uint gTileIdx;                     // 近景のぼけの最大(タイル)の SRV 番号
    uint gNeighborIdx;                 // 周囲 3x3 のタイルの最大の SRV 番号
    uint gFarIdx;                      // 遠景のギャザーの結果の SRV 番号
    uint gNearIdx;                     // 近景のギャザーの結果(rgb=色、a=カバー率)の SRV 番号
    uint gPad0;
    uint gPad1;
};

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

// 異常値(NaN・無限大・負)は 0、極端に大きい値は fp16 の上限の手前で止める。
float3 Sanitize(float3 c) {
    c = select(isfinite(c), c, float3(0.0, 0.0, 0.0));
    return clamp(c, 0.0, 60000.0);
}

float InterleavedGradientNoise(float2 pixel, uint frame) {
    pixel += 5.588238 * float(frame & 7u);
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// フル解像度の画素のぼけの半径[フル解像度の画素]。符号は、フォーカスより手前(近景)が負、奥(遠景)が正。
// 錯乱円の直径 c = f^2 / (N (s - f)) * |d - s| / d(DepthOfFieldMath.h の CocDiameterMm と同じ式)。
// d は視線方向の奥行き。深度が遠方面(Reversed-Z のクリア値 0)の画素は d = 無限大で、c = f^2 / (N (s - f))。
float SignedCocRadius(int2 pixel) {
    Texture2D<float> depthTex = ResourceDescriptorHeap[gDepthIdx];
    float depth = depthTex.Load(int3(pixel, 0));
    float scale = gFocalMm * gFocalMm / (gAperture * (gFocusMm - gFocalMm));
    float coc = scale;
    float side = 1.0;
    if (depth > 0.0) {
        float2 ndc = float2((float(pixel.x) + 0.5) * gInvSize.x * 2.0 - 1.0, 1.0 - (float(pixel.y) + 0.5) * gInvSize.y * 2.0);
        float4 world = mul(float4(ndc, depth, 1.0), gInvViewProj);
        // 逆行列を掛けた w は、クリップ空間の w(視線方向の距離)の逆数。
        float depthMm = max(1.0 / max(abs(world.w), 1.0e-9), 1.0e-6) * 1000.0;
        coc = scale * abs(depthMm - gFocusMm) / depthMm;
        side = depthMm < gFocusMm ? -1.0 : 1.0;
    }
    float radius = min(0.5 * coc * gMmToPx, gMaxRadius);
    return isfinite(radius) ? side * radius : 0.0;
}

// 1) プリフィルタ。2x2 のフル解像度の画素から、色は輝度の重みで平均し、ぼけの半径は、近景が1つでもあれば最も負の値、
// 無ければ平均にする。
float4 PSPrefilter(VSOut input) : SV_Target {
    Texture2D<float4> colorTex = ResourceDescriptorHeap[gColorIdx];
    int2 p = int2(input.pos.xy);
    int2 maxPixel = int2(gSize) - 1;
    float3 sum = float3(0.0, 0.0, 0.0);
    float weight = 0.0;
    float minCoc = 0.0;
    float sumCoc = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        int2 q = min(p * 2 + int2(i & 1, i >> 1), maxPixel);
        float3 c = Sanitize(colorTex.Load(int3(q, 0)).rgb);
        float w = 1.0 / (1.0 + dot(c, float3(0.2126, 0.7152, 0.0722)));
        sum += c * w;
        weight += w;
        float coc = SignedCocRadius(q);
        minCoc = min(minCoc, coc);
        sumCoc += coc;
    }
    return float4(sum / weight, minCoc < 0.0 ? minCoc : sumCoc * 0.25);
}

// 近景のぼけの大きさ[半解像度の画素]。負(近景)の側だけで、奥や 0 は 0。
float NearMagnitude(float signedCocFull) { return max(-signedCocFull, 0.0) * 0.5; }

// 2) タイルごとの最大の近景のぼけ。横(半解像度の横をタイル幅に縮めた RT へ)→ 縦(タイルの RT へ)の2回に分ける。
float2 PSTilePartial(VSOut input) : SV_Target {
    Texture2D<float4> prefilterTex = ResourceDescriptorHeap[gPrefilterIdx];
    int2 p = int2(input.pos.xy);
    int maxX = int(gHalfSize.x) - 1;
    float best = 0.0;
    for (uint i = 0; i < gTileSize; ++i) {
        int x = min(p.x * int(gTileSize) + int(i), maxX);
        best = max(best, NearMagnitude(prefilterTex.Load(int3(x, p.y, 0)).a));
    }
    return float2(best, 0.0);
}

float2 PSTileMax(VSOut input) : SV_Target {
    Texture2D<float2> partialTex = ResourceDescriptorHeap[gPartialIdx];
    int2 p = int2(input.pos.xy);
    int maxY = int(gHalfSize.y) - 1;
    float best = 0.0;
    for (uint i = 0; i < gTileSize; ++i) {
        int y = min(p.y * int(gTileSize) + int(i), maxY);
        best = max(best, partialTex.Load(int3(p.x, y, 0)).x);
    }
    return float2(best, 0.0);
}

// 3) 周囲 3x3 のタイルの最大。画素へ届く近景のぼけは、自分のタイルと隣のタイルのどれかから来る。
float2 PSNeighborMax(VSOut input) : SV_Target {
    Texture2D<float2> tileTex = ResourceDescriptorHeap[gTileIdx];
    int2 p = int2(input.pos.xy);
    int2 maxTile = int2(ceil(gHalfSize / float(gTileSize))) - 1;
    float best = 0.0;
    [unroll] for (int dy = -1; dy <= 1; ++dy) {
        [unroll] for (int dx = -1; dx <= 1; ++dx) {
            int2 q = clamp(p + int2(dx, dy), int2(0, 0), maxTile);
            best = max(best, tileTex.Load(int3(q, 0)).x);
        }
    }
    return float2(best, 0.0);
}

// 円盤の i 番目の点の、中心からのずれ[半解像度の画素]。黄金角のらせん(面積が一様)を、画素ごとの位相でずらす。
float2 DiskOffset(uint i, float radius, float phase) {
    float angle = float(i) * 2.39996323 + phase;
    float r = radius * sqrt((float(i) + 0.5) / float(gSamples));
    return r * float2(cos(angle), sin(angle));
}

// 4) 遠景のギャザー。自分の遠景のぼけの半径の円盤で拾う。点の重みは、その点自身の遠景のぼけの半径が距離に届くかで決め、
// 手前の点(近景・ピントが合った点)は拾わない。
float4 PSFar(VSOut input) : SV_Target {
    Texture2D<float4> prefilterTex = ResourceDescriptorHeap[gPrefilterIdx];
    int2 p = int2(input.pos.xy);
    int2 maxPixel = int2(gHalfSize) - 1;
    float4 center = prefilterTex.Load(int3(p, 0));
    float radius = max(center.a, 0.0) * 0.5;
    if (radius < 0.5) return float4(center.rgb, 1.0);

    float phase = InterleavedGradientNoise(float2(p), gFrame) * (2.0 * acos(-1.0));
    float3 sum = center.rgb;
    float weight = 1.0;
    for (uint i = 0; i < gSamples; ++i) {
        int2 q = clamp(int2(floor(float2(p) + 0.5 + DiskOffset(i, radius, phase))), int2(0, 0), maxPixel);
        float dist = length(float2(q - p));
        float4 s = prefilterTex.Load(int3(q, 0));
        float cocFar = s.a > 0.0 ? s.a * 0.5 : 0.0;
        float w = saturate(cocFar - dist + 1.0);
        sum += s.rgb * w;
        weight += w;
    }
    return float4(sum / weight, 1.0);
}

// 5) 近景のギャザー。周囲のタイルの最大のぼけの半径の円盤で拾う。点の重みは、その点自身の近景のぼけの半径が距離に
// 届くかで決める。rgb は重みで平均した色、a は円盤のうち近景に覆われた割合(縁が外へ柔らかく広がる)。
float4 PSNear(VSOut input) : SV_Target {
    Texture2D<float4> prefilterTex = ResourceDescriptorHeap[gPrefilterIdx];
    Texture2D<float2> neighborTex = ResourceDescriptorHeap[gNeighborIdx];
    int2 p = int2(input.pos.xy);
    int2 maxPixel = int2(gHalfSize) - 1;
    float4 center = prefilterTex.Load(int3(p, 0));
    float radius = neighborTex.Load(int3(p / int(gTileSize), 0)).x;
    if (radius < 0.5) return float4(center.rgb, 0.0);

    float phase = InterleavedGradientNoise(float2(p), gFrame) * (2.0 * acos(-1.0));
    float centerWeight = NearMagnitude(center.a) > 0.0 ? saturate(NearMagnitude(center.a) + 1.0) : 0.0;
    float3 sum = center.rgb * centerWeight;
    float weight = centerWeight;
    for (uint i = 0; i < gSamples; ++i) {
        int2 q = clamp(int2(floor(float2(p) + 0.5 + DiskOffset(i, radius, phase))), int2(0, 0), maxPixel);
        float dist = length(float2(q - p));
        float4 s = prefilterTex.Load(int3(q, 0));
        float mag = NearMagnitude(s.a);
        float w = mag > 0.0 ? saturate(mag - dist + 1.0) : 0.0; // 近景のぼけを持つ点だけが覆う
        sum += s.rgb * w;
        weight += w;
    }
    if (weight <= 0.0) return float4(center.rgb, 0.0);
    return float4(sum / weight, saturate(weight / float(gSamples) * 2.0));
}

// 半解像度の RT を、フル解像度の画素の中心の位置でバイリニアに引く(Load 4回)。
float4 SampleHalfBilinear(Texture2D<float4> tex, int2 fullPixel) {
    float2 pos = (float2(fullPixel) + 0.5) * 0.5 - 0.5;
    int2 base = int2(floor(pos));
    float2 f = pos - float2(base);
    int2 maxPixel = int2(gHalfSize) - 1;
    float4 c00 = tex.Load(int3(clamp(base, int2(0, 0), maxPixel), 0));
    float4 c10 = tex.Load(int3(clamp(base + int2(1, 0), int2(0, 0), maxPixel), 0));
    float4 c01 = tex.Load(int3(clamp(base + int2(0, 1), int2(0, 0), maxPixel), 0));
    float4 c11 = tex.Load(int3(clamp(base + int2(1, 1), int2(0, 0), maxPixel), 0));
    return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}

// 6) 合成。遠景のぼけの画素は、半解像度の遠景の結果と混ぜる(混ぜる割合は自分のぼけの半径で決める)。
// 近景の画素は遠景のぼけと混ぜず、その上に近景の結果をカバー率で重ねる。
float4 PSComposite(VSOut input) : SV_Target {
    Texture2D<float4> colorTex = ResourceDescriptorHeap[gColorIdx];
    Texture2D<float4> farTex = ResourceDescriptorHeap[gFarIdx];
    Texture2D<float4> nearTex = ResourceDescriptorHeap[gNearIdx];
    int2 p = int2(input.pos.xy);
    float3 color = Sanitize(colorTex.Load(int3(p, 0)).rgb);
    float cocFull = SignedCocRadius(p);
    float t = cocFull > 0.0 ? saturate((cocFull - 0.5) / 1.5) : 0.0;
    float3 result = lerp(color, Sanitize(SampleHalfBilinear(farTex, p).rgb), t);
    float4 nearColor = SampleHalfBilinear(nearTex, p);
    result = lerp(result, Sanitize(nearColor.rgb), saturate(nearColor.a));
    return float4(result, 1.0);
}
)";

} // namespace rs::render
