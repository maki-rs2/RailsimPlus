//
// HeadlightFlare.h - 灯具(PI の Headlight)のレンズフレアを、LensFlareFrame へ足す純関数
//
// RailSim II の CHeadlight::Render と同じ式。灯具の位置・向きはワールド座標で受け取る(求めるのは
// アプリ層の収集側)。光源は無限遠でない点光源として frame.lights へ足し、シーンの深度で遮蔽する。
// GPU には依存しない。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "LensFlarePass.h"

namespace rs::render {

// 1フレームに出す灯具1件。style はフレームの間だけ生きていればよいので共有ポインタで持つ。
struct HeadlightLight {
    rs::core::Vec3 position;
    rs::core::Vec3 direction;  // 灯具の向き(正規化済みでなくてよい)
    float maxDistance = 0.0f;  // Headlight の MaxDistance
    std::shared_ptr<const FlareStyle> style;
};

// 灯具ごとに ComputeFlareAlpha → ApplyTwinkle → LayoutFlareElements で要素を求め、要素が出たものだけ
// 光源と要素を frame に足す。上限(kMaxFlareLights・kMaxFlareInstances。frame に既にある太陽の分は残す)を
// 超えるときは、カメラに近い灯具を優先して残す。上限で捨てた灯具の数を返す。
std::size_t AppendHeadlightFlares(const std::vector<HeadlightLight>& lights, const rs::core::Vec3& cameraPos,
                                  const rs::core::Vec3& cameraForward, std::uint32_t frameIndex,
                                  LensFlareFrame& frame);

} // namespace rs::render
