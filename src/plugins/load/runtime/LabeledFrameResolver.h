//
// LabeledFrameResolver.h - ラベル付きフレームの実行時ワールド行列を解決する。
//
#pragma once

#include <string>
#include <vector>

#include "../../../core/Math.h"

namespace rs::app {

// name が指すラベルの今フレームのワールド行列を引く。
// labels[i]/resolved[i]/world[i] は同じインデックスで対応する並列配列。
// システム予約名は恒等行列、未解決なら false、未知の名前は恒等行列で続行する。
bool FindLabeledFrame(const std::string& name, const std::vector<std::string>& labels,
                      const std::vector<bool>& resolved,
                      const std::vector<rs::core::Mat4>& world, rs::core::Mat4& outMat);

} // namespace rs::app
