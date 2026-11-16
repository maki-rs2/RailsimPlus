#pragma once

namespace rs::render {

// グリフ1文字分の配置情報とアトラス上のUV。GPU型に依存しないため頂点生成側からも参照できる。
struct GlyphInfo {
    float widthPx = 0.0f;
    float heightPx = 0.0f;
    float bearingXPx = 0.0f;
    float bearingYPx = 0.0f;
    float advancePx = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

} // namespace rs::render
