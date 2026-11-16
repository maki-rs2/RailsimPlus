#pragma once

namespace rs::world {

// 信号の速度制限を表す表示色(RGB、各0〜1)。
struct SignalColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

// 速度[km/h]から信号の色を求める。0=赤、45=黄、120=緑、160=青を区間ごとに線形補間し、
// 範囲外は端の色にする。
SignalColor SignalSpeedColor(double speedKmh);

} // namespace rs::world
