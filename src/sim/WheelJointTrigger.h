//
// WheelJointTrigger.h - 車輪音を鳴らす継目の通過判定(純関数)
//
// 車軸の通算走行距離を継目間隔で割った値を四捨五入し、フレームの前後で値が変わったときに
// 1回鳴らす。つまり (k+0.5)*継目間隔 を跨いだ瞬間。逆走でも同じ判定になる。
//
#pragma once

#include <optional>

namespace rs::sim {

// 音の素材の先頭にある無音の長さ[ms]。再生の開始位置はこの長さから遅れ補正を引いた位置になる。
inline constexpr double kWheelSoundLeadInMs = 100.0;

// 距離 distanceBefore -> distanceAfter [m] の1フレームで継目を跨いだかを返す。
// 跨いだときは素材の再生開始位置[ms]（kWheelSoundLeadInMs - 遅れ補正）を返す。
// 1フレームで複数の継目を跨いでも1回だけ。jointIntervalMeters が 0 以下・NaN のとき、
// および距離が有限でないときは鳴らさない。frameMs は実フレーム時間[ms]。
std::optional<double> DetectWheelJointCrossing(double distanceBefore, double distanceAfter,
                                               double jointIntervalMeters, double frameMs);

} // namespace rs::sim
