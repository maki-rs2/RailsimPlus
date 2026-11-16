//
// SunFlareFrame.h - 太陽のレンズフレアと白飛びの、1フレーム分の入力を組み立てる(CPU側。GPUに依存しない)
//
// 太陽の光源は、カメラから太陽の向きへ kSunFlareDistanceM の仮想の点に置き、無限遠として遮蔽を判定する。
// 灯具は、これと同じ LensFlareFrame へ光源(atInfinity=false)と要素を足して同じパスで描く。
//
#pragma once

#include <cstdint>

#include "../../core/Math.h"
#include "LensFlarePass.h"
#include "SunFlareSettings.h"

namespace rs::render {

struct SunFlareFrameInput {
    rs::core::Vec3 cameraPos;
    rs::core::Vec3 cameraForward;  // カメラの前方(正規化しなくてよい)
    rs::core::Vec3 directionToSun;  // カメラから見た太陽の向き(正規化しなくてよい。長さ0なら何も出さない)
    float vol = 0.0f;               // 全体の強さ(昼夜係数。夜は0)
    std::uint32_t frameIndex = 0;   // またたきの乱数のシード
    bool flareEnabled = true;       // 効果スイッチ(SunLensFlare)
    bool whiteoutEnabled = true;    // 効果スイッチ(SunWhiteout)
};

// frame の光源・要素・白飛びを、太陽の分で置き換える(視点・画面の大きさ・フレーム番号は呼び出し側が入れる)。
// frame.draws[].instance.element は settings.style の要素を指す。frame は settings より長く持たない。
void BuildSunFlareFrame(const SunFlareSettings& settings, const SunFlareFrameInput& input, LensFlareFrame& frame);

} // namespace rs::render
