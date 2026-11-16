//
// LandscapeParams.h - 標準の空の遠景を光で照らす値(調整値→シェーダの定数)
//
// IblGenerator(環境ソースの焼き込み)が作る。背景(SkyPass)には遠景を合成しない。HLSL側は
// LandscapeShader.h の SampleLandscape が、同名の cbuffer 変数(gLandscape*)から読む。
//
#pragma once

#include <numbers>

#include "../resource/RenderTuning.h"

namespace rs::render {

struct LandscapeLightingParams {
    float tint[3] = {1.0f, 1.0f, 1.0f};
    float saturation = 1.0f;
    float brightness = 1.0f;
    float contrast = 1.0f;
    float yawRad = 0.0f;
};

inline LandscapeLightingParams MakeLandscapeLightingParams(const RenderTuning& tuning) {
    LandscapeLightingParams params;
    params.tint[0] = tuning.landscapeTintR;
    params.tint[1] = tuning.landscapeTintG;
    params.tint[2] = tuning.landscapeTintB;
    params.saturation = tuning.landscapeSaturation;
    params.brightness = tuning.landscapeBrightness;
    params.contrast = tuning.landscapeContrast;
    params.yawRad = tuning.landscapeYawDeg * std::numbers::pi_v<float> / 180.0f;
    return params;
}

} // namespace rs::render
