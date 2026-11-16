//
// ShaderColor.h - CPU と HLSL で共有する色と色空間変換
//
#pragma once

#include <array>
#include <cstdint>

namespace rs::render {

// Rec.709 の輝度重み。CPU の写真統計は double、描画用の基準実装は float を使う。
template <typename T>
inline constexpr std::array<T, 3> kRec709LuminanceWeights = {
    static_cast<T>(0.2126), static_cast<T>(0.7152), static_cast<T>(0.0722)};

// ORM 未指定時の1画素。AO=1、roughness=0.8、metallic=0、alpha=1(0xAABBGGRR)。
inline constexpr std::uint32_t kDefaultOrmRGBA = 0xFF00CCFFu;

inline constexpr const char* kColorConstantsShader = R"(
// リニアRGBの Rec.709 と、FXAA がガンマ近似に使う Rec.601 の重みを区別する。
static const float3 kRec709LuminanceWeights = float3(0.2126, 0.7152, 0.0722);
static const float3 kRec601LuminanceWeights = float3(0.299, 0.587, 0.114);
static const float3 kDefaultOrm = float3(1, 0.8, 0);
)";

inline constexpr const char* kSrgbTransferShader = R"(
float3 LinearFromSrgb(float3 color) {
    float3 lower = color / 12.92;
    float3 upper = pow((color + 0.055) / 1.055, 2.4);
    return float3(color.r <= 0.04045 ? lower.r : upper.r,
                  color.g <= 0.04045 ? lower.g : upper.g,
                  color.b <= 0.04045 ? lower.b : upper.b);
}

// HDR値(1超)も符号なしのpowでそのまま延長する。負値は0に丸める。
float3 SrgbFromLinear(float3 color) {
    float3 c = max(color, 0.0);
    float3 lower = c * 12.92;
    float3 upper = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return float3(c.r <= 0.0031308 ? lower.r : upper.r,
                  c.g <= 0.0031308 ? lower.g : upper.g,
                  c.b <= 0.0031308 ? lower.b : upper.b);
}
)";

} // namespace rs::render
