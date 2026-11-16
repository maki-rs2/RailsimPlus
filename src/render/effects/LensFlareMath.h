//
// LensFlareMath.h - レンズフレアの位置・明るさ・白飛びの強さを求める純関数
//
// RailSim II の CLensFlare::Render / CWhiteout::Render と同じ式。描画(GPU)には依存しない。
// 太陽と灯具で共通。違いは呼び出し側が渡す光源の位置・向き・最大距離だけ。
// 定義の型(PI定義の LensFlare{})は plugins 層にあるので、render 層は自前の FlareStyle で受ける
// (度→コサイン、Distance の昇順への並べ替えは、定義から FlareStyle を作る側で済ませる)。
//
#pragma once

#include <cstdint>
#include <vector>

#include "../../core/Math.h"

namespace rs::render {

// 太陽の光源は、カメラからこの距離の仮想の点に置く[m]。Distance・Radius はこの距離を基準にした値。
constexpr float kSunFlareDistanceM = 100.0f;

enum class FlareElementKind { Circle, Hexagon, Texture };

struct FlareColor {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

struct FlareElementSpec {
    FlareElementKind kind = FlareElementKind::Circle;
    float distance = 0.0f;
    float radius = 0.0f;
    FlareColor innerColor;
    FlareColor outerColor;
    std::uint32_t textureIndex = 0;  // Texture のときだけ使う(使う側が決める番号)
};

struct FlareStyle {
    float startCos = 1.0f;  // StartAngle のコサイン
    float twinkle = 0.0f;
    float inclination = 0.0f;
    std::vector<FlareElementSpec> elements;  // Distance の昇順
};

// 描くフレア要素1件。brightness は暗くする係数(色に掛ける)。
struct FlareInstance {
    rs::core::Vec3 position;
    float radius = 0.0f;
    float brightness = 0.0f;
    const FlareElementSpec* element = nullptr;
};

// フレア全体の明るさ(またたき前)。光軸の正面から StartAngle 以内でなければ 0。
// dir: 光源の向き、toward: 光源へ向かう向き(どちらも正規化しなくてよい)。vol: 全体の強さ(0〜1)。
float ComputeFlareAlpha(float startCos, const rs::core::Vec3& dir, const rs::core::Vec3& toward, float vol);

// 毎フレームのまたたき。alpha に FRand2(1 - twinkle, 1) を掛ける。random01 は 0〜1 の乱数。
float ApplyTwinkle(float alpha, float twinkle, float random01);

// フレーム番号と番号違いのシードから 0〜1 の値を作る毎回同じ結果になるハッシュ(同じ入力は同じ値)。
float HashRandom01(std::uint32_t frameIndex, std::uint32_t seed);

// 要素の配置。光源 pos から approach 方向へ、Distance の差 × range ずつ進める。
// maxDistance < 0 なら列に沿った減衰なし(太陽)。brightness が 0 以下になった要素で打ち切る。
// 戻り値の element は style.elements を指す(style より長く持たない)。
std::vector<FlareInstance> LayoutFlareElements(const FlareStyle& style, float alpha, const rs::core::Vec3& pos,
                                                const rs::core::Vec3& dir, const rs::core::Vec3& cameraPos,
                                                const rs::core::Vec3& cameraForward, float maxDistance);

// 太陽の仮想の点(カメラ位置 + 太陽へ向かう向き × kSunFlareDistanceM)。
rs::core::Vec3 SunFlarePosition(const rs::core::Vec3& cameraPos, const rs::core::Vec3& directionToSun);

// 白飛びの強さ(0〜1)。カメラ前方と太陽のなす角が StartAngle 以内のときだけ 0 より大きい。
float ComputeWhiteoutAlpha(float startCos, const rs::core::Vec3& cameraForward, const rs::core::Vec3& directionToSun,
                           float vol);

} // namespace rs::render
