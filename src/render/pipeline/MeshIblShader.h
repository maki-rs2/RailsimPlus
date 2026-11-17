//
// MeshIblShader.h - メッシュのシェーダ(ShadeSurface)が使う、環境光(IBL)・太陽色の HLSL 関数
//
// MeshShader.h の cbuffer Constants の後ろ、ShadeSurface の前に連結する(cbuffer の gIbl* を使う)。
// テクスチャは bindless テーブルの配列で引く(SRV番号は cbuffer で渡る。EnvironmentState)。
// 透過率LUTの引き方(TransmittanceParamsToUv)は大気LUTと同じ式を共有する(AtmosphereShader.h)。
//
#pragma once

#include <string>

#include "../sky/AtmosphereShader.h"
#include "../sky/SunColorShader.h"

namespace rs::render {

inline constexpr const char* kMeshIblFunctionsShader = R"HLSL(
// s1: リニア・クランプ(ルート署名 MeshRootSignature.cpp の静的サンプラ)。
SamplerState gIblSmp : register(s1);

// 直射光に掛ける太陽色(式は SunColorShader.h)。toSun は太陽へ向かう単位ベクトル(Y-up)。
float3 MeshSunColor(float3 toSun) {
    Texture2D<float4> lut = gBindlessTex2D[gIblIndex.w];
    return SunColorFromTransmittance(lut, gIblSmp, toSun);
}

// 太陽が物を照らす直射光(太陽色×強さの調整値。式は SunColorShader.h)。月の光には MeshSunColor を使う。
float3 MeshSunLight(float3 toSun) {
    Texture2D<float4> lut = gBindlessTex2D[gIblIndex.w];
    return SunLightFromTransmittance(lut, gIblSmp, toSun);
}

// プラグインの空(Envの焼き済みキューブ)を使うか。IBLの有効判定は呼び出し側(ShadeSurface)が掛ける。
bool IsPluginSky() {
    return gPluginDirectional.w > 0.5;
}

// 拡散IBL: 法線方向の「放射照度÷π」。albedo を掛けると拡散の環境光になる。
// プラグインの空のときは焼き済みの拡散キューブ × 光の倍率 × pluginSkyDiffuseIntensity(生成IBLの倍率は掛けない)。
float3 SampleIblDiffuse(float3 n) {
    bool plugin = IsPluginSky();
    TextureCube<float4> cube = gBindlessTexCube[plugin ? gPluginIblIndex.x : gIblIndex.x];
    float3 scale = plugin ? gPluginMeshLightScale.rgb * gIblParams2.y : float3(1.0, 1.0, 1.0);
    return cube.SampleLevel(gIblSmp, n, 0).rgb * scale;
}

// 鏡面IBL(split-sum): 鏡面キューブ(R, ミップ=roughness*(ミップ数-1)) × (F0*A + B)。
// 標準の空のミップには iblSpecularMipScale(gIblParams2.w)を掛ける(1.0で焼き方どおり、0.5で RailSim II -k-build と同じ鋭さ)。
// f90 はグレージング角の反射率(通常1。F0が黒の材質は0へ寄せて鏡面を残さない)。albedo は掛けない。強さ(iblSpecularIntensity)は掛け済み。
// プラグインの空のときは焼き済みの鏡面キューブ(× 光の倍率)・BRDF LUT・pluginSkySpecularIntensity(生成IBLの倍率は掛けない)。
// プラグインの空の鏡面キューブは、RailSim II -k-build のPBRシェーダが roughness × min(ミップ数, 4) でミップを引く
// (どれだけ粗い材質でもミップ4まで)ので、同じ上限で頭打ちにする。
static const float kPluginSkyMaxSpecularMip = 4.0;

float3 SampleIblSpecular(float3 r, float ndv, float roughness, float3 f0, float f90) {
    bool plugin = IsPluginSky();
    TextureCube<float4> cube = gBindlessTexCube[plugin ? gPluginIblIndex.y : gIblIndex.y];
    Texture2D<float4> brdfLut = gBindlessTex2D[plugin ? gPluginIblIndex.z : gIblIndex.z];
    float mipCount = plugin ? gPluginAmbient.w : gIblParams.x;
    float intensity = plugin ? gIblParams2.z : gIblParams.z;
    float maxMip = plugin ? min(mipCount - 1.0, kPluginSkyMaxSpecularMip) : (mipCount - 1.0) * gIblParams2.w;
    float3 prefiltered = cube.SampleLevel(gIblSmp, r, roughness * maxMip).rgb;
    if (plugin) prefiltered *= gPluginMeshLightScale.rgb;
    // 焼き済みのBRDF LUTは縦軸が逆向き(先頭行=roughness 1、最終行=roughness 0)。生成側は先頭行=roughness 0。
    float lutV = plugin ? 1.0 - roughness : roughness;
    float2 ab = brdfLut.SampleLevel(gIblSmp, float2(ndv, lutV), 0).xy;
    return prefiltered * (f0 * ab.x + f90 * ab.y) * intensity;
}
)HLSL";

// 定数部(kBottomRadius等) + 透過率のUV割り当て + 太陽色 + メッシュ用のIBL関数。
inline std::string BuildMeshIblShader() {
    return BuildAtmosphereConstantsShader() + kTransmittanceUvShader + kSunColorShader + kMeshIblFunctionsShader;
}

} // namespace rs::render
