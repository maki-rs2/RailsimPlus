//
// AmbientOcclusionShader.h - GTAO(Ground Truth Ambient Occlusion)と、その空間ぼかしの HLSL シェーダ文字列
//
// 入力は深度プリパスの深度(R32_FLOAT、Reversed-Z、距離は視空間 z = B / (d - A))。法線は深度から復元する。
// 各画素で画面上の向き(スライス)を回し、片側ずつ水平線の角度(いちばん高く見える遮蔽物)を探して、
// 法線の向きで重みを付けた余弦積分で「見える割合」を求める(Jimenez ら 2016)。
// 向きと刻みのずらしは 4x4 の Bayer 並びで、ぼかしがその 4x4 をちょうど平均して並びを消す。
// 出力は見える割合(0〜1、1=遮蔽なし)の R8。
//
#pragma once

namespace rs::render {

inline constexpr const char* kAmbientOcclusionShader = R"(
Texture2D<float> gDepth : register(t0);
Texture2D<float> gRaw : register(t1);

cbuffer AoConstants : register(b0) {
    float2 gAoInvSize;   // 1 / AO の大きさ(画素)
    float2 gDepthSize;   // 深度の大きさ(画素)
    float2 gTanHalf;     // (tan(水平視野/2), tan(垂直視野/2))
    float2 gDepthLin;    // (A, B): 視空間 z = B / (d - A)
    float gRadius;       // 遮蔽を探す半径[m]
    float gFalloffMul;   // 距離の重み = saturate(距離 * Mul + Add)
    float gFalloffAdd;
    float gFinalPower;
    float gProjScale;    // 深度の高さ[画素] / (2 * tan(垂直視野/2))。視空間の長さ[m] を画面上の画素数へ換える係数 / z
    uint gSliceCount;
    uint gStepCount;
    uint gScale;         // AO 1画素が深度の何画素に当たるか(1か2)
};

static const float kPi = acos(-1.0);
static const float kHalfPi = 0.5 * kPi;
static const float kMaxRadiusPixels = 160.0;  // 画面上の探索半径の上限(近いほど半径が大きくなりすぎる)
static const float kMinStepPixels = 1.3;      // 最初の刻みの下限(画素)。自分の画素を拾わないため
static const float kBayer[16] = {0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0};

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint vid : SV_VertexID) {
    VSOut o;
    float2 p = float2((vid << 1) & 2, vid & 2); // (0,0),(2,0),(0,2)
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float ViewZ(float d) { return gDepthLin.y / (d - gDepthLin.x); }

// 深度の画素 px の視空間の位置(左手系、z が奥)。画面の外は深度0(遠方)として読まれ、遮蔽にならない。
float3 ViewPosAt(int2 px) {
    float z = ViewZ(gDepth.Load(int3(px, 0)));
    float2 uv = (float2(px) + 0.5) / gDepthSize;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return float3(ndc * gTanHalf * z, z);
}

// 周囲4画素のうち、奥行きの差が小さいほうの向きで面の法線を求める(輪郭をまたがないため)。カメラ側が正。
float3 ReconstructNormal(int2 px, float3 pc) {
    float3 pl = ViewPosAt(px + int2(-1, 0));
    float3 pr = ViewPosAt(px + int2(1, 0));
    float3 pu = ViewPosAt(px + int2(0, -1));
    float3 pd = ViewPosAt(px + int2(0, 1));
    float3 h = abs(pc.z - pl.z) < abs(pr.z - pc.z) ? (pc - pl) : (pr - pc);
    float3 v = abs(pc.z - pu.z) < abs(pd.z - pc.z) ? (pc - pu) : (pd - pc);
    return normalize(cross(h, v));
}

float Bayer4(int2 p) { return (kBayer[(p.x & 3) + (p.y & 3) * 4] + 0.5) / 16.0; }

float PSGtao(VSOut i) : SV_TARGET {
    const int2 aoPx = int2(i.pos.xy);
    const int2 px = aoPx * int(gScale);
    if (gDepth.Load(int3(px, 0)) <= 0.0) return 1.0; // 何も描かれていない画素(空)
    const float3 P = ViewPosAt(px);
    const float3 N = ReconstructNormal(px, P);
    const float3 V = normalize(-P);

    const float pixelRadius = min(gRadius * gProjScale / P.z, kMaxRadiusPixels);
    if (pixelRadius < kMinStepPixels) return 1.0; // 遠くて半径が1画素に満たない
    const float minStep = kMinStepPixels / pixelRadius;
    const float noiseSlice = Bayer4(aoPx);
    const float noiseStep = Bayer4(aoPx + int2(2, 1));

    float visibility = 0.0;
    [loop] for (uint slice = 0; slice < gSliceCount; ++slice) {
        const float phi = (float(slice) + noiseSlice) / float(gSliceCount) * kPi;
        const float2 omega = float2(cos(phi), -sin(phi));       // 画面上の向き(y は下が正)
        const float3 dir3 = float3(cos(phi), sin(phi), 0.0);    // 視空間の向き(y は上が正)
        const float3 orthoDir = dir3 - dot(dir3, V) * V;
        const float3 axis = normalize(cross(orthoDir, V));
        const float3 projN = N - axis * dot(N, axis);
        const float projLen = length(projN);
        const float cosN = saturate(dot(projN, V) / max(projLen, 1e-5));
        const float n = sign(dot(orthoDir, projN)) * acos(cosN);
        const float lowCos0 = cos(n + kHalfPi);
        const float lowCos1 = cos(n - kHalfPi);
        float horizonCos0 = lowCos0;
        float horizonCos1 = lowCos1;
        [loop] for (uint stepIndex = 0; stepIndex < gStepCount; ++stepIndex) {
            float s = (float(stepIndex) + noiseStep) / float(gStepCount);
            s = min(s * s + minStep, 1.0);
            const int2 offset = int2(round(omega * (s * pixelRadius)));
            const float3 delta0 = ViewPosAt(px + offset) - P;
            const float3 delta1 = ViewPosAt(px - offset) - P;
            const float len0 = length(delta0);
            const float len1 = length(delta1);
            const float weight0 = saturate(len0 * gFalloffMul + gFalloffAdd);
            const float weight1 = saturate(len1 * gFalloffMul + gFalloffAdd);
            const float shc0 = lerp(lowCos0, dot(delta0 / max(len0, 1e-5), V), weight0);
            const float shc1 = lerp(lowCos1, dot(delta1 / max(len1, 1e-5), V), weight1);
            horizonCos0 = max(horizonCos0, shc0);
            horizonCos1 = max(horizonCos1, shc1);
        }
        float h0 = -acos(clamp(horizonCos1, -1.0, 1.0));
        float h1 = acos(clamp(horizonCos0, -1.0, 1.0));
        h0 = n + clamp(h0 - n, -kHalfPi, kHalfPi);
        h1 = n + clamp(h1 - n, -kHalfPi, kHalfPi);
        const float arc0 = (cosN + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) * 0.25;
        const float arc1 = (cosN + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) * 0.25;
        visibility += projLen * (arc0 + arc1);
    }
    visibility = pow(saturate(visibility / float(gSliceCount)), gFinalPower);
    return max(visibility, 0.03); // 真っ黒にしない
}

// 4x4 の窓を平均する(Bayer の並びをちょうど1周)。奥行きの差が大きい画素は混ぜない。
float PSBlur(VSOut i) : SV_TARGET {
    const int2 aoPx = int2(i.pos.xy);
    const int2 aoSize = int2(1.0 / gAoInvSize + 0.5);
    const float zCenter = ViewZ(gDepth.Load(int3(aoPx * int(gScale), 0)));
    const float tolerance = max(zCenter * 0.04, 0.05);
    float sum = 0.0;
    float weightSum = 0.0;
    [unroll] for (int y = -2; y < 2; ++y) {
        [unroll] for (int x = -2; x < 2; ++x) {
            const int2 q = aoPx + int2(x, y);
            if (any(q < 0) || any(q >= aoSize)) continue;
            const float z = ViewZ(gDepth.Load(int3(q * int(gScale), 0)));
            const float w = saturate(1.0 - abs(z - zCenter) / tolerance);
            sum += gRaw.Load(int3(q, 0)) * w;
            weightSum += w;
        }
    }
    return weightSum > 0.0 ? sum / weightSum : gRaw.Load(int3(aoPx, 0));
}
)";

} // namespace rs::render
