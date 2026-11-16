//
// Color.h - 共用RGBAカラー型(0.0〜1.0の正規化float)。
// float diffuse[4] 等の即席表現を各所で持たないための共通型。
// 描画パイプライン非依存(DirectX型に変換する境界は呼び出し側で1箇所に集約する)。
//
#pragma once

namespace rs::core {

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    Color() = default;
    Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    bool operator!=(const Color& other) const { return !(*this == other); }
};

}  // namespace rs::core
