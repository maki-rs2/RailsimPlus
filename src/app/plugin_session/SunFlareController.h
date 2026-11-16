//
// SunFlareController.h - 現在のEnvが変わったとき、太陽のレンズフレアと白飛びの設定を作り直す
//
// ActiveEnv の Generation を見て、変わったときだけ設定(SunFlareSetup)を作り、テクスチャ要素の画像を
// 台帳(FlareTextureSlots。灯具と共用)へ通して、新しく割り当てた画像を LensFlarePass へ上げる。
// 画像は数枚の小さなものなので、読み込みは呼び出したスレッドで行う。
//
#pragma once

#include <cstdint>

#include "../../render/effects/LensFlarePass.h"
#include "../../render/effects/SunFlareSettings.h"
#include "ActiveEnv.h"
#include "FlareTextureSlots.h"

namespace rs::app {

class SunFlareController {
public:
    // Env が前回と変わっていたら、settings を作り直し、新しい画像を pass へ上げる。毎フレーム呼んでよい。
    void Update(const ActiveEnv& activeEnv, FlareTextureSlots& slots, rs::render::LensFlarePass& pass,
                rs::render::SunFlareSettings& settings);

private:
    std::uint64_t seenGeneration_ = 0;
};

} // namespace rs::app
