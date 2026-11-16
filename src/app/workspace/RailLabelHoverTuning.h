#pragma once

namespace rs::app::workspace {

struct RailLabelHoverTuning {
    // レールからラベルへポインタを移すため、レールから外れた後も表示を保つ時間[秒]。
    double graceSeconds = 0.3;

    static const RailLabelHoverTuning& Embedded();
};

} // namespace rs::app::workspace
