//
// PassSizing.h - 画面の大きさに依存する資源を持つパスの、Ready / EnsureSize の呼び方をそろえる
//
// パスごとに Ready(w,h)+Resize(w,h)、Ready()+Width()+Height()+Resize(w,h) などと形が違うので、
// 呼び出し側は TargetSize を1つ渡す IsSizeReady / EnsureSize だけを使う。
//   IsSizeReady : その大きさで使える資源が揃っているか
//   EnsureSize  : 揃っていなければ作り直し、使える状態になったか(true/false)を返す。足りているときは何もしない
// 呼ぶ前に、GPU がそのパスの資源を使い終えていること。
//
#pragma once

#include <concepts>
#include <cstdint>

#include "../../render/postprocess/AmbientOcclusionPass.h"
#include "../../render/postprocess/AoQuality.h"

namespace rs::app {

struct TargetSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Ready(w,h) と Resize(w,h) を持つパス(TAA・モーションブラー)。
template <class Pass>
concept SizedPass = requires(Pass& pass, std::uint32_t w, std::uint32_t h) {
    { pass.Ready(w, h) } -> std::convertible_to<bool>;
    { pass.Resize(w, h) } -> std::convertible_to<bool>;
};

// Ready() と Width()・Height()、Resize(w,h) を持つ描画先(HDRシーン・プリパスの深度・速度バッファ)。
template <class Target>
concept SizedTarget = requires(Target& target, const Target& constTarget, std::uint32_t w, std::uint32_t h) {
    { constTarget.Ready() } -> std::convertible_to<bool>;
    { constTarget.Width() } -> std::convertible_to<std::uint32_t>;
    { constTarget.Height() } -> std::convertible_to<std::uint32_t>;
    { target.Resize(w, h) } -> std::convertible_to<bool>;
};

template <SizedPass Pass>
bool IsSizeReady(const Pass& pass, TargetSize size) {
    return pass.Ready(size.width, size.height);
}

template <SizedPass Pass>
bool EnsureSize(Pass& pass, TargetSize size) {
    return pass.Ready(size.width, size.height) || pass.Resize(size.width, size.height);
}

template <SizedTarget Target>
bool IsSizeReady(const Target& target, TargetSize size) {
    return target.Ready() && target.Width() == size.width && target.Height() == size.height;
}

template <SizedTarget Target>
bool EnsureSize(Target& target, TargetSize size) {
    return IsSizeReady(target, size) || target.Resize(size.width, size.height);
}

// アンビエントオクルージョンは品質で資源の大きさが変わる。品質が「なし」のときは何も作らず、Ready にもならない。
inline bool IsSizeReady(const rs::render::AmbientOcclusionPass& pass, TargetSize size) {
    return pass.Ready(size.width, size.height);
}

inline bool EnsureSize(rs::render::AmbientOcclusionPass& pass, TargetSize size, rs::render::AoQuality quality) {
    // Resize は、同じ大きさ・品質なら何もしない。
    return pass.Resize(size.width, size.height, quality);
}

} // namespace rs::app
