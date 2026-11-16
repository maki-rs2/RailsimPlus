//
// MeshFogShader.h - メッシュに大気遠近(aerial perspective。AP)と高さフォグをかける HLSL
//
// MeshShader.h の cbuffer Constants(gCameraPos・gAp*・gHeightFogParams)と MeshShadeSurfaceShader.h(SurfaceShading・
// AdaptationGainFromTexture・IblExposureCorrection)・MeshIblShader.h(IsPluginSky・SampleIblDiffuse・gIblSmp)の
// 後ろに連結する。呼び出しは ApplyAerialPerspective → ApplyHeightFog の順(c' = (c·T_ap + S_ap)·T_hf + S_hf)。
// APのLUT(AerialPerspectiveLut)は、距離方向のスライスを横に並べた1枚の2Dテクスチャ。
// rgb=カメラからその距離までに散乱された光、a=透過率。ShadeSurface が返した色に
//   拡散側 = 拡散側 × 透過率 + 散乱光 × 空の明るさ
//   鏡面側 = 鏡面側 × 透過率
// を当てる。空のピクセルには当てない(空パスは大気を通した色を直接描く)。
//
// 式の対応(変えるときは両方):
//   画面方向      AtmosphereMath.cpp の AerialViewDirection の逆
//   距離→位置     AerialUnitFromDistanceKm(距離 = 最大距離 × 位置^2)
//   アトラス座標  AerialAtlasU(タイルの端のテクセル中心へ収め、隣のスライスへにじませない)
//   LUTの生成     AerialPerspectiveShader.h の AerialPS
// 距離の単位は、ワールドがメートル、LUTがkm。
//
// 高さフォグ: 密度 ρ(h) = ρ0 × exp(-(h - h0) / H)。カメラから画素までの光学的厚さ τ を式で解き(レイマーチなし)、
//   透過率 T = exp(-τ)、散乱光 S = (1 - T) × 霧の反射率 × 霧に当たる光(上向きの拡散IBL)
//   拡散側 = 拡散側 × T + S、鏡面側 = 鏡面側 × T
// を、AP のあとに当てる。
// ボリュメトリックフォグが有効なフレームは、格子の範囲内を格子の結果に置き換え、範囲より遠くだけ式で求める。
//   拡散側 = 拡散側 × T_格子 × T_遠方 + S_遠方 × T_格子 + S_格子、鏡面側 = 鏡面側 × T_格子 × T_遠方
// 遠方の S には影なしの太陽の散乱も足して、格子の端で見え方をつなげる。
//   τの式          HeightFogMath.cpp の OpticalDepth(変えるときは両方。しきい値の定数も同じ値)
//
#pragma once

namespace rs::render {

inline constexpr const char* kMeshFogShader = R"HLSL(
// 散乱光に掛ける、空の背景と同じ明るさの倍率。gApScaleAndFog.x は空の背景の放射輝度の倍率そのもの
// (標準の空は skyRadianceScale、プラグインの空は skyRadianceScale × pluginSkyAtmosphereScale。FrameRenderer が入れる)。
// 標準の空だけ、空パスと同じく暗順応の倍率を掛ける(プラグインの空に暗順応はない)。
float3 AerialScatterScale() {
    float scale = gApScaleAndFog.x;
    if (gIblFlags.x == 0u || IsPluginSky()) return float3(scale, scale, scale);
    return float3(scale, scale, scale) * AdaptationGainFromTexture();
}

// 0..1の位置を、LUTの端のテクセル中心の内側へ収める(AtmosphereShader.h の SubUvFromUnit と同じ)。
float ApSubUvFromUnit(float u, float resolution) { return (u * (resolution - 1.0) + 0.5) / resolution; }

void ApplyAerialPerspective(inout SurfaceShading shading, float3 wpos) {
    if (gApIndex.w == 0u) return;

    float3 toPixel = wpos - gCameraPos.xyz;
    float distanceMeters = length(toPixel);
    if (distanceMeters <= 0.0) return;
    float3 dir = toPixel / distanceMeters;

    // 画面方向(0..1。unitY は上から下)。カメラの後ろ・真横に近い画素は0割りを避けて中央に置く。
    float forwardDot = dot(dir, gApForward.xyz);
    float unitX = 0.5;
    float unitY = 0.5;
    if (forwardDot > 1e-3) {
        float x = dot(dir, gApRight.xyz) / (forwardDot * gApRight.w);
        float y = -dot(dir, gApUp.xyz) / (forwardDot * gApUp.w);
        unitX = saturate(x * 0.5 + 0.5);
        unitY = saturate(y * 0.5 + 0.5);
    }

    // 奥行き。距離 = 最大距離 × 位置^2。最後のスライスより遠い画素は最後のスライスで止まる。
    float sliceCount = float(gApIndex.y);
    float tileSize = float(gApIndex.z);
    float unitZ = sqrt(saturate(distanceMeters * 0.001 / gApForward.w));
    float slicePos = unitZ * (sliceCount - 1.0);
    float slice0 = floor(slicePos);
    float slice1 = min(slice0 + 1.0, sliceCount - 1.0);
    float blend = slicePos - slice0;

    float atlasWidth = sliceCount * tileSize;
    float inTileU = ApSubUvFromUnit(unitX, tileSize) * tileSize;
    float v = ApSubUvFromUnit(unitY, tileSize);
    Texture2D<float4> lut = ResourceDescriptorHeap[gApIndex.x];
    float4 fogA = lut.SampleLevel(gIblSmp, float2((slice0 * tileSize + inTileU) / atlasWidth, v), 0);
    float4 fogB = lut.SampleLevel(gIblSmp, float2((slice1 * tileSize + inTileU) / atlasWidth, v), 0);
    float4 fog = lerp(fogA, fogB, blend);

    float transmittance = fog.a;
    shading.diffuseSide = shading.diffuseSide * transmittance + fog.rgb * AerialScatterScale();
    shading.specularSide = shading.specularSide * transmittance;
}

// 霧に当たる光(albedo を掛ける前の放射輝度)。法線(0,1,0)の拡散IBL。夜の床・AO は掛けない。
float3 HeightFogLight() {
    // IBL無効: 固定の環境光(AmbientLight の最後の分岐と同じく、時刻で弱める)。
    if (gIblFlags.x == 0u) return float3(1.0, 1.0, 1.0) * (kAmbientIntensity * saturate(gLightDir.w));
    float3 up = float3(0.0, 1.0, 0.0);
    // プラグインの空: 焼き済みの拡散キューブ。光の倍率と pluginSkyDiffuseIntensity は SampleIblDiffuse の中で掛け済みなので、
    // ここでは掛けない。
    if (IsPluginSky()) return SampleIblDiffuse(up);
    // 標準の空: ShadeSurface の standardSky 分岐と同じ。暗順応の倍率はキューブ側に掛け済みなので掛けない。
    return gIblParams.y * SampleIblDiffuse(up) * IblExposureCorrection();
}

// 視線に沿った光学的厚さ。HeightFogMath.cpp の OpticalDepth と同じ式(変えるときは両方)。
static const float kHeightFogExponentLimit = 30.0;   // HeightFogMath.h の kExponentLimit
static const float kHeightFogSmallSlope = 1.0e-3;    // HeightFogMath.h の kSmallSlope
float HeightFogOpticalDepth(float density, float heightScale, float baseHeight, float cameraHeight, float pixelHeight,
                            float distanceMeters) {
    float a = clamp((cameraHeight - baseHeight) / heightScale, -kHeightFogExponentLimit, kHeightFogExponentLimit);
    float b = clamp((pixelHeight - baseHeight) / heightScale, -kHeightFogExponentLimit, kHeightFogExponentLimit);
    float slope = b - a;
    float meanDensityFactor = abs(slope) < kHeightFogSmallSlope ? exp(-(a + b) * 0.5)
                                                                : (exp(-a) - exp(-b)) / slope;
    return max(density * distanceMeters * meanDensityFactor, 0.0);
}

// 格子の散乱と同じ単位の、影なしの太陽の光(霧の反射率を掛ける前)。dir は視線の向き。
float3 VolumetricFogSunLight(float3 dir) {
    float3 toSun = normalize(-gLightDir.xyz);
    float phase = VolumetricFogPhase(dot(dir, toSun), gVolumetricFogParams.y);
    return kPi * MeshSunColor(toSun) * AdaptationGainFromTexture() * phase * gVolumetricFogParams.z;
}

void ApplyVolumetricFog(inout SurfaceShading shading, float3 wpos) {
    float4 clip = mul(float4(wpos, 1.0), gViewProj);
    float viewDepth = clip.w;
    if (viewDepth <= 0.0) return;
    float maxDistance = gVolumetricFogParams.x;
    float2 ndc = clip.xy / clip.w;
    float3 unit = float3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, VolumetricFogSliceUnit(viewDepth, maxDistance));
    Texture3D<float4> integrated = ResourceDescriptorHeap[gVolumetricFogIndex.x];
    float4 grid = integrated.SampleLevel(gIblSmp, unit, 0);

    float farTransmittance = 1.0;
    float3 farScatter = float3(0.0, 0.0, 0.0);
    if (viewDepth > maxDistance) {
        float3 toPixel = wpos - gCameraPos.xyz;
        float distanceMeters = length(toPixel);
        float boundary = maxDistance / viewDepth;
        float boundaryHeight = gCameraPos.y + toPixel.y * boundary;
        float opticalDepth = HeightFogOpticalDepth(gApScaleAndFog.y, gApScaleAndFog.z, gApScaleAndFog.w, boundaryHeight,
                                                   wpos.y, distanceMeters * (1.0 - boundary));
        farTransmittance = exp(-opticalDepth);
        farScatter = (1.0 - farTransmittance) * asfloat(gHeightFogParams.w) *
                     (HeightFogLight() + VolumetricFogSunLight(toPixel / distanceMeters));
    }
    float transmittance = grid.a * farTransmittance;
    shading.diffuseSide = shading.diffuseSide * transmittance + farScatter * grid.a + grid.rgb;
    shading.specularSide = shading.specularSide * transmittance;
}

void ApplyHeightFog(inout SurfaceShading shading, float3 wpos) {
    if (gHeightFogParams.z == 0u) return;
    if (gVolumetricFogIndex.x != 0u) {
        ApplyVolumetricFog(shading, wpos);
        return;
    }
    float distanceMeters = length(wpos - gCameraPos.xyz);
    float opticalDepth = HeightFogOpticalDepth(gApScaleAndFog.y, gApScaleAndFog.z, gApScaleAndFog.w, gCameraPos.y, wpos.y,
                                               distanceMeters);
    float transmittance = exp(-opticalDepth);
    float3 scatter = (1.0 - transmittance) * asfloat(gHeightFogParams.w) * HeightFogLight();
    shading.diffuseSide = shading.diffuseSide * transmittance + scatter;
    shading.specularSide = shading.specularSide * transmittance;
}
)HLSL";

} // namespace rs::render
