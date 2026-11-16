//
// StepMath.h - 段の値(昇順の数列)から値に近い段を選ぶ純関数
//
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rs::core {

// 段の値 steps(昇順の正の値)のうち、value に対数で最も近いものの番号。無限大は最後の段。steps が空なら -1。
// 遠い距離ほど段が粗いので、差は比(対数)で比べる。
inline int NearestLogStepIndex(const std::vector<double>& steps, double value) {
    if (steps.empty()) return -1;
    if (!std::isfinite(value) || value >= steps.back()) return static_cast<int>(steps.size()) - 1;
    int best = 0;
    double bestGap = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const double gap = std::abs(std::log(steps[i] / std::max(value, 1e-9)));
        if (gap < bestGap) {
            bestGap = gap;
            best = static_cast<int>(i);
        }
    }
    return best;
}

} // namespace rs::core
