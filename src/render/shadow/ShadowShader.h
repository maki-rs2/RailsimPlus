//
// ShadowShader.h - 影を受ける側の HLSL(b1 の影定数、比較サンプラ、太陽の直射光に掛ける係数)
//
// kShadowConstantsShader は影定数と比較サンプラとカスケードの選択で、メッシュのシェーダと compute が共用する。
// kShadowReceiveShader は受ける面の係数で、MeshShader.h の cbuffer Constants(gCameraPos を使う)と
// kShadowConstantsShader の後ろ、MeshShadeSurfaceShader.h の前に連結する。
// 影の係数は太陽の直射光(拡散と鏡面)にだけ掛け、環境光・IBL・発光・月の直射光には掛けない。
// 影定数の並びは ShadowConstants.h と一致させる。
//
#pragma once

namespace rs::render {

inline constexpr const char* kShadowConstantsShader = R"HLSL(
cbuffer ShadowConstants : register(b1) {
    row_major float4x4 gShadowViewProj[4];
    float4 gShadowSplitFar;     // 各カスケードの奥側のカメラ前方向の距離[m]
    float4 gShadowTexelWorld;   // 各カスケードのテクセル1つのワールド幅[m]
    // x=影を使うか(0/1) y=影の強さ(時刻での減衰を掛けた後) z=法線オフセット[テクセル]
    // w=カスケード境界のブレンド範囲の幅(受け持つ距離に対する割合。0ならブレンドしない)
    float4 gShadowParams;
    // x=シャドウ距離[m] y=遠方のフェードを始める距離[m] z=ぼけ幅(半径)[m] w=ぼかしのタップ数(0なら1タップ)
    float4 gShadowParams2;
    uint4 gShadowIndex;         // x=シャドウマップ(Texture2DArray)のSRV絶対番号 y=カスケード数
    // PCSS: x=光源の大きさ(遮蔽物との距離に対するぼけ半径の比) y=ぼけ半径の上限[m](遮蔽物を探す半径も同じ)
    // z=遮蔽物とみなす最小の深度差[m] w=PCSSを使うか(0/1)
    float4 gShadowParams3;
};
// 比較サンプラ(LESS_EQUAL、2x2のハードウェアPCF)。参照深度がマップの深度以下なら1(日なた)。
// マップの外はボーダー(1.0)なので、外のタップは日なたとして数える。
SamplerComparisonState gShadowSmp : register(s2);

// カメラ前方向の距離から、受け持つカスケードを選ぶ。
uint ShadowCascadeIndex(float viewDepth) {
    uint last = max(gShadowIndex.y, 1u) - 1u;
    uint cascade = 0u;
    while (cascade < last && viewDepth > gShadowSplitFar[cascade]) ++cascade;
    return cascade;
}
)HLSL";

inline constexpr const char* kShadowReceiveShader = R"HLSL(
// 単位円の中のタップ位置(ひまわり状に敷いた8点と16点)。ピクセルごとの角度で回して使う。
static const float2 kShadowTaps8[8] = {
    float2(0.2500, 0.0000), float2(-0.3193, 0.2925), float2(0.0489, -0.5569), float2(0.4024, 0.5249),
    float2(-0.7385, -0.1306), float2(0.6996, -0.4450), float2(-0.2340, 0.8705), float2(-0.4463, -0.8593)
};
static const float2 kShadowTaps16[16] = {
    float2(0.1768, 0.0000), float2(-0.2258, 0.2068), float2(0.0346, -0.3938), float2(0.2846, 0.3712),
    float2(-0.5222, -0.0924), float2(0.4947, -0.3147), float2(-0.1655, 0.6155), float2(-0.3156, -0.6076),
    float2(0.6846, 0.2500), float2(-0.7123, 0.2940), float2(0.3434, -0.7337), float2(0.2537, 0.8089),
    float2(-0.7647, -0.4432), float2(0.8971, -0.1972), float2(-0.5475, 0.7788), float2(-0.1265, -0.9761)
};

// 回転角用の乱数[0,1)。ワールド座標を1cm格子に丸めてハッシュするので、止まった面ではちらつかない。
float ShadowRotationNoise(float3 wpos) {
    uint3 cell = asuint(int3(floor(wpos * 100.0)));
    uint h = cell.x * 73856093u ^ cell.y * 19349663u ^ cell.z * 83492791u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return float(h & 0xffffffu) * (1.0 / 16777216.0);
}

// カスケード1枚の、ライト方向の奥行き(正射影の深度0〜1に対応する長さ)[m]。
// 行列のz列の長さが 1/奥行き(ライト空間の軸は単位長)。
float ShadowCascadeDepthRangeM(uint cascade) {
    float3 zColumn = float3(gShadowViewProj[cascade][0][2], gShadowViewProj[cascade][1][2], gShadowViewProj[cascade][2][2]);
    return 1.0 / max(length(zColumn), 1e-6);
}

// PCSS: 遮蔽物の探索。受ける点のまわり(上限の半径)の深度を比較なしで読み、受ける点より手前にあるものの
// 平均の距離[m]から、ぼけ半径を決める(遠い遮蔽物ほど広く)。遮蔽物がなければ0(日なた)を返す。
float ShadowPcssBlurRadius(Texture2DArray<float> shadowMap, uint slice, float2 uv, float depth, float2 mapSize,
                           float2 rot) {
    float range = ShadowCascadeDepthRangeM(slice);
    float searchTexels = gShadowParams3.y / max(gShadowTexelWorld[slice], 1e-4);
    float gapSum = 0.0;
    float blockerCount = 0.0;
    for (uint i = 0; i < 8u; ++i) {
        float2 tap = kShadowTaps8[i];
        float2 offsetTexels = float2(tap.x * rot.x - tap.y * rot.y, tap.x * rot.y + tap.y * rot.x) * searchTexels;
        int2 texel = int2(floor(uv * mapSize + offsetTexels));
        if (any(texel < 0) || any(texel >= int2(mapSize))) continue;
        float gap = (depth - shadowMap.Load(int4(texel, slice, 0))) * range;
        if (gap > gShadowParams3.z) {
            gapSum += gap;
            blockerCount += 1.0;
        }
    }
    if (blockerCount < 0.5) return 0.0;
    return clamp(gapSum / blockerCount * gShadowParams3.x, gShadowParams2.z, gShadowParams3.y);
}

// 回転Poissonディスクのぼかし。ぼけ半径[m]をテクセルの世界幅で割ってテクセル数にし、uv空間へ直して引く。
// PCSS(gShadowParams3.w)のときは、ぼけ半径を遮蔽物の探索で決める(遮蔽物がなければ日なた)。
float SampleShadowBlur(Texture2DArray<float> shadowMap, uint slice, float2 uv, float depth, float3 wpos, uint tapCount) {
    float2 mapSize;
    float elements;
    shadowMap.GetDimensions(mapSize.x, mapSize.y, elements);
    float angle = ShadowRotationNoise(wpos) * 6.28318530718;
    float2 rot = float2(cos(angle), sin(angle));
    float radiusM = gShadowParams2.z;
    if (gShadowParams3.w > 0.5) {
        radiusM = ShadowPcssBlurRadius(shadowMap, slice, uv, depth, mapSize, rot);
        if (radiusM <= 0.0) return 1.0;
    }
    float radiusTexels = radiusM / max(gShadowTexelWorld[slice], 1e-4);
    float2 radiusUv = radiusTexels / mapSize;
    float sum = 0.0;
    for (uint i = 0; i < tapCount; ++i) {
        float2 tap = (tapCount <= 8u) ? kShadowTaps8[i] : kShadowTaps16[i];
        float2 offset = float2(tap.x * rot.x - tap.y * rot.y, tap.x * rot.y + tap.y * rot.x) * radiusUv;
        sum += shadowMap.SampleCmpLevelZero(gShadowSmp, float3(uv + offset, slice), depth);
    }
    return sum / float(tapCount);
}

// カスケード1枚の可視率(1=日なた)。法線方向へテクセルの世界幅に比例してずらして引き、
// 斜めの面の自己影(シャドウアクネ)を抑える。奥行きがマップの範囲の外なら日なた。
// ぼかさないときの uv の外も日なた。ぼかすときの uv の外は、タップごとにボーダー(日なた)で数える。
float SampleShadowCascade(Texture2DArray<float> shadowMap, uint cascade, float3 wpos, float3 vertexNormal,
                          float sinTheta, uint tapCount) {
    // 法線オフセットの長さ。テクセルの世界幅に比例させるが、ぼかすときはぼけ幅の半分を下限にする。
    // 近いカスケードはテクセルが細かく、ぼけ幅(ワールド長で一定)が相対的に広くなって、傾いた面が自分の影に入るため。
    float normalOffset = gShadowTexelWorld[cascade] * gShadowParams.z;
    if (tapCount > 0u) normalOffset = max(normalOffset, 0.5 * gShadowParams2.z);
    float3 offsetPos = wpos + vertexNormal * (normalOffset * sinTheta);
    float4 clip = mul(float4(offsetPos, 1.0), gShadowViewProj[cascade]);
    float2 uv = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
    if (clip.z < 0.0 || clip.z > 1.0) return 1.0;
    if (tapCount == 0u) {
        if (any(uv < 0.0) || any(uv > 1.0)) return 1.0;
        return shadowMap.SampleCmpLevelZero(gShadowSmp, float3(uv, cascade), clip.z);
    }
    return SampleShadowBlur(shadowMap, cascade, uv, clip.z, wpos, tapCount);
}

// 太陽の直射光に掛ける係数 1 - (1 - v) * s(v=1が日なた・0が完全な影、s=影の強さ)。
// 影を使わない描画(サムネイルなど、gShadowParams.x=0)・シャドウ距離の外・光に背を向けた面は1。
// vertexNormal は法線マップを掛ける前の頂点法線。カスケードはカメラ前方向の距離で選び、
// 境界のブレンド範囲では隣の枚と混ぜる。
float SunShadowScale(float3 wpos, float3 vertexNormal, float3 toLight) {
    if (gShadowParams.x < 0.5) return 1.0;
    float cosTheta = dot(vertexNormal, toLight);
    if (cosTheta <= 0.0) return 1.0;
    float distanceFade = saturate((gShadowParams2.x - length(gCameraPos.xyz - wpos)) /
                                  max(gShadowParams2.x - gShadowParams2.y, 1e-3));
    if (distanceFade <= 0.0) return 1.0;

    float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
    uint tapCount = (uint)gShadowParams2.w;
    Texture2DArray<float> shadowMap = gBindlessTex2DArrR[gShadowIndex.x];

    float viewDepth = mul(float4(wpos, 1.0), gViewProj).w;
    uint last = max(gShadowIndex.y, 1u) - 1u;
    uint cascade = ShadowCascadeIndex(viewDepth);
    float visibility = SampleShadowCascade(shadowMap, cascade, wpos, vertexNormal, sinTheta, tapCount);
    if (cascade < last && gShadowParams.w > 0.0) {
        float rangeNear = (cascade > 0u) ? gShadowSplitFar[cascade - 1u] : 0.0;
        float blendWidth = max(gShadowParams.w * (gShadowSplitFar[cascade] - rangeNear), 1e-3);
        float blend = saturate((viewDepth - (gShadowSplitFar[cascade] - blendWidth)) / blendWidth);
        if (blend > 0.0) {
            float nextVisibility =
                SampleShadowCascade(shadowMap, cascade + 1u, wpos, vertexNormal, sinTheta, tapCount);
            visibility = lerp(visibility, nextVisibility, blend);
        }
    }
    return 1.0 - (1.0 - visibility) * gShadowParams.y * distanceFade;
}
)HLSL";

} // namespace rs::render
