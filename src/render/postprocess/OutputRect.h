//
// OutputRect.h - 最終の出力先(バックバッファ)の中の矩形[ピクセル]
//
// 幅・高さが0以下なら「矩形の指定なし」(出力先の全体)として扱う。
//
#pragma once

namespace rs::render {

struct OutputRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

} // namespace rs::render
