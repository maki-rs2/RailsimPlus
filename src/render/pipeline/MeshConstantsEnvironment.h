//
// MeshConstantsEnvironment.h - 環境光(IBL)・太陽色(EnvironmentState)と調整値(RenderTuning)から MeshConstants を詰める関数。
//
// EnvironmentState・RenderTuning は変更が多く、読む側が広がりやすい。MeshConstants.h から分けて、
// 環境や調整値を使わない .cpp が取り込まないようにする。
//
#pragma once

#include "EnvFlag.h"
#include "MeshConstants.h"
#include "../resource/RenderTuning.h"
#include "../sky/EnvironmentState.h"

namespace rs::render {

// 環境光(IBL)・太陽色の設定を MeshConstants へ詰める。FrameRenderer が毎フレーム1か所で呼ぶ。
inline void ApplyEnvironmentToMeshConstants(const EnvironmentState& env, MeshConstants& dst) {
    dst.iblIndex[0] = env.srv.diffuse;
    dst.iblIndex[1] = env.srv.specular;
    dst.iblIndex[2] = env.srv.brdfLut;
    dst.iblIndex[3] = env.srv.transmittance;
    dst.iblParams[0] = static_cast<float>(env.specularMipCount);
    dst.iblParams[1] = env.iblDiffuseIntensity;
    dst.iblParams[2] = env.iblSpecularIntensity;
    dst.iblParams[3] = env.envMapMetallic;
    dst.iblParams2[0] = env.envMapRoughness;
    dst.iblParams2[1] = env.pluginSkyDiffuseIntensity;
    dst.iblParams2[2] = env.pluginSkySpecularIntensity;
    dst.iblParams2[3] = env.iblSpecularMipScale;
    dst.iblFlags[0] = env.iblEnabled ? 1u : 0u;
    dst.iblFlags[1] = static_cast<uint32_t>(env.debugMode);
    dst.iblFlags[2] = PbrUnifiedEnabled() ? 1u : 0u;
    for (int i = 0; i < 3; ++i) {
        dst.pluginDirectional[i] = env.pluginDirectional[i];
        dst.pluginAmbient[i] = env.pluginAmbient[i];
        dst.pluginMeshLightScale[i] = env.pluginMeshLightScale[i];
    }
    dst.pluginDirectional[3] = env.pluginSky ? 1.0f : 0.0f;
    dst.pluginAmbient[3] = static_cast<float>(env.pluginSpecularMipCount);
    dst.pluginIblIndex[0] = env.pluginSrv.diffuse;
    dst.pluginIblIndex[1] = env.pluginSrv.specular;
    dst.pluginIblIndex[2] = env.pluginSrv.brdfLut;
    for (int i = 0; i < 3; ++i) dst.moonLight[i] = env.moonDirection[i];
    dst.moonLight[3] = env.moonRatio;
    dst.adaptation[0] = env.srv.exposure;
    dst.adaptation[1] = env.adaptation ? 1u : 0u;
}

// 本編とサムネイルが共通で入れる定数(光の向き・昼夜係数・環境)。光が進む向きを lightDirXyz に渡す。
// 視点は ApplyViewToMeshConstants、フォグ(高さ・空気遠近)は呼び出し側が続けて入れる。
inline MeshConstants MakeBaseMeshConstants(const float* lightDirXyz, float dayAlpha, const EnvironmentState& env) {
    MeshConstants c = {};
    for (int i = 0; i < 3; ++i) {
        c.lightDir[i] = lightDirXyz[i];
        c.diffuse[i] = kMeshDefaultDiffuseRgb;
    }
    c.lightDir[3] = dayAlpha; // 昼夜係数(1=昼、0=完全な夜)
    c.diffuse[3] = 1.0f;
    ApplyEnvironmentToMeshConstants(env, c);
    return c;
}

// 埋め込みの調整値(RenderTuning)から高さフォグの入力を作る。本編とサムネイルが同じ値を使うための1か所。
inline HeightFogFrame MakeHeightFogFrame(const RenderTuning& tuning, bool enabled) {
    HeightFogFrame frame;
    frame.enabled = enabled;
    frame.density = tuning.heightFogDensity;
    frame.heightScale = tuning.heightFogHeightScale;
    frame.baseHeight = tuning.heightFogBaseHeight;
    frame.albedo = tuning.heightFogAlbedo;
    return frame;
}

} // namespace rs::render
