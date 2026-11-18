#pragma once

#include <optional>

#include "../workspace/RailLabelHover.h"

namespace rs::app {

// 入力側が毎フレーム更新し、オーバーレイとラベルの表示側が読む。
struct SignalBlockViewportLink {
    std::optional<workspace::RailLabelAnchor> hoveredRail;
};

} // namespace rs::app
