//
// MeshBrdfShader.h - メッシュのシェーダ(ShadeSurface)が使う、資源を引かない BRDF の部品の HLSL 関数
//
// 直射光の鏡面(Cook-Torrance: GGX の D と、高さ相関の Smith の可視項 V)・太陽の円盤による分布の広げ方・
// 多重散乱のエネルギー補正・法線マップの接線の計算。cbuffer や SRV には依存しない
// (MeshShadeSurfaceShader.h が、cbuffer・MeshIblShader.h の後ろに連結する)。
//
// 単位: 直射光の放射照度は π × sunColor。白いランバート面の放射輝度は albedo × sunColor × ndl
// (拡散の式。ShadeSurface)。鏡面は BRDF × π × sunColor × ndl なので、BRDF に π を掛けて返す。
//
#pragma once

#include <string>

#include "../sky/GgxShader.h"

namespace rs::render {

inline constexpr const char* kMeshBrdfFunctionsShader = R"HLSL(
// 太陽の角半径 kSunAngularRadius は大気の定数部(AtmosphereShader.h の BuildAtmosphereConstantsShader)が定義する。
// 角半径のうち、GGXの alpha(粗さの幅)に足す割合。円盤の縁まで広げると山が広すぎるので半分にする。
static const float kSunDiskAlphaRatio = 0.5;

// 太陽の円盤の大きさで分布を広げる(点光源のままだと、つるつるの面のハイライトが1画素以下になる)。
float SunDiskWidenedAlpha(float alpha) {
    return saturate(alpha + kSunDiskAlphaRatio * kSunAngularRadius);
}

// 高さ相関の Smith GGX の可視項 V = G / (4 ndl ndv)。alpha = roughness^2。
static const float kVisibilityDenominatorMin = 1e-5;
float VisibilitySmithGgxCorrelated(float ndl, float ndv, float alpha) {
    float a2 = alpha * alpha;
    float gv = ndl * sqrt(ndv * ndv * (1.0 - a2) + a2);
    float gl = ndv * sqrt(ndl * ndl * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, kVisibilityDenominatorMin);
}

// 直射光の鏡面の係数(sunColor・dayAlpha は掛けない)。放射輝度 = これ × sunColor。
// = D(α') × V × F × π × ndl。α' は太陽の円盤で広げた分布の幅。
float3 DirectSpecularGgx(float3 fresnel, float ndl, float ndv, float ndh, float roughness) {
    float alpha = roughness * roughness;
    float d = DistributionGgxAlpha(ndh, SunDiskWidenedAlpha(alpha));
    float v = VisibilitySmithGgxCorrelated(ndl, ndv, alpha);
    return fresnel * (d * v * kPi * ndl);
}

// 多重散乱のエネルギー補正(Filament と同じ近似)。ab は BRDF LUT の (A, B)、f0 は本来の垂直反射率。
// 単一散乱の鏡面(split-sum の F0*A + B)が失うエネルギーを、粗い面ほど多く足し戻す。
static const float kEnergyCompensationAbMin = 1e-3;
float3 EnergyCompensation(float3 f0, float2 ab) {
    return 1.0 + f0 * (1.0 / max(ab.x + ab.y, kEnergyCompensationAbMin) - 1.0);
}

// 法線マップの接線フレーム(タンジェント無し頂点なので画面微分で作る)。
// UVが画面空間で片軸に潰れている(または、UVの行列式がほぼゼロ)箇所は T がゼロベクトルになり、
// normalize が NaN を生んで後続の法線計算全体に伝播する(極端に狭いUVマッピングで描画がおかしくなる)。
// そのときは偽を返し、呼び出し側が頂点法線のまま使う。
// UVが反転(ミラー)した面は、行列式の符号で T・B の向きを直す(凹凸の向きが逆にならない)。
static const float kTangentMinLength = 1e-8;
static const float kUvDeterminantMin = 1e-12;
bool ComputeTangentFrame(float3 wpos, float2 uv, float3 n, out float3 t, out float3 b) {
    t = n;
    b = n;
    float3 dp1 = ddx(wpos), dp2 = ddy(wpos);
    float2 du1 = ddx(uv), du2 = ddy(uv);
    float det = du1.x * du2.y - du2.x * du1.y;
    float3 tRaw = dp1 * du2.y - dp2 * du1.y;
    if (length(tRaw) <= kTangentMinLength || abs(det) <= kUvDeterminantMin) return false;
    float uvSign = det < 0.0 ? -1.0 : 1.0;
    float3 dPdv = (dp2 * du1.x - dp1 * du2.x) * uvSign;
    float3 tangent = normalize(tRaw) * uvSign;
    tangent = tangent - n * dot(n, tangent); // 頂点法線に直交させる
    if (length(tangent) <= kTangentMinLength) return false;
    t = normalize(tangent);
    b = cross(n, t);
    if (dot(b, dPdv) < 0.0) b = -b;
    return true;
}
)HLSL";

// GGX の D + BRDF の部品。
inline std::string BuildMeshBrdfShader() {
    return std::string(kGgxDistributionShader) + kMeshBrdfFunctionsShader;
}

} // namespace rs::render
