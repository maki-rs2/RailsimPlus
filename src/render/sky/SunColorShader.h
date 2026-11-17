//
// SunColorShader.h - 太陽の色(観測者から太陽方向への透過率)の HLSL 関数
//
// メッシュの直射光(MeshIblShader.h の MeshSunColor)・空の太陽の円盤・遠景の光(LandscapeShader.h)が
// 同じ式を使う。AtmosphereShader.h の定数部と kTransmittanceUvShader の後ろへ連結する。
// DXC(SM6.0)でもFXC(SM5)でも通る形(テクスチャとサンプラを引数で受ける)。
//
#pragma once

namespace rs::render {

inline constexpr const char* kSunColorShader = R"HLSL(
// 太陽の円盤のうち、地平線(地球の縁)より上に出ている割合(0〜1、地球の影)。観測者の高さから見た地平線の向きの y を中心に、
// 円盤の角半径の幅で 0 から 1 へ変える。透過率LUTは大気の上端までの透過率で、地面にさえぎられるかを含まないので、ここで掛ける。
float SunVisibilityAboveHorizon(float toSunY) {
    float viewHeight = kBottomRadius + kObserverHeight;
    float radiusRatio = kBottomRadius / viewHeight;
    float horizonY = -sqrt(max(0.0, 1.0 - radiusRatio * radiusRatio));
    float halfWidth = sin(kSunAngularRadius);
    return smoothstep(horizonY - halfWidth, horizonY + halfWidth, toSunY);
}

// 直射光に掛ける太陽色。観測者(地表付近の固定の高さ)から太陽方向への透過率(0〜1)× 地球の影。
// 空の放射輝度は大気の外の太陽(放射照度π)から求めているので、直射光にも同じ透過率を掛けて、
// 空と直射光の明るさの比を物理に合わせる(正午でもやや暗く、夕方は赤橙になり、沈むと0になる)。
// toSun は太陽へ向かう単位ベクトル(Y-up)。
float3 SunColorFromTransmittance(Texture2D<float4> transmittanceLut, SamplerState smp, float3 toSun) {
    float viewHeight = kBottomRadius + kObserverHeight;
    float3 transmittance =
        saturate(transmittanceLut.SampleLevel(smp, TransmittanceParamsToUv(viewHeight, toSun.y), 0).rgb);
    return transmittance * SunVisibilityAboveHorizon(toSun.y);
}

// 太陽が物を照らす直射光の色と強さ(標準の空)。太陽色に倍率 kSunLightScale(大気の定数部。空と同じ倍率×調整値の比)を掛ける。
// メッシュ・粒子・遠景・霧の太陽の光はこれを使い、太陽の円盤・レンズフレア(見た目の明るさは別の調整値)と月は
// SunColorFromTransmittance をそのまま使う。
float3 SunLightFromTransmittance(Texture2D<float4> transmittanceLut, SamplerState smp, float3 toSun) {
    return SunColorFromTransmittance(transmittanceLut, smp, toSun) * kSunLightScale;
}
)HLSL";

} // namespace rs::render
