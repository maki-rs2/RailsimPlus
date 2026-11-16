//
// SolarGeometry.h - 太陽(SolarPosition)と月(MoonPosition)で共有する天球の幾何の部品。
//
// 季節の位相・強制モードの解釈・赤緯と時角から高度/方位/方向ベクトルを出す式を1か所に置き、
// 太陽と月で同じ式を2か所に書かないようにする。GUI・DirectX非依存。計算はdouble。
//
#pragma once

#include <optional>

#include "GameClock.h"
#include "SolarPosition.h"

namespace rs::world {

// 観測者から見た方向(北=+Z、東=+X、+Yが天頂。azimuthRadは北が0で東回りに増加)。
struct HorizontalDirection {
    double x = 0.0;
    double y = 1.0;
    double z = 0.0;
    double elevationRad = 0.0;
    double azimuthRad = 0.0;
};

// 季節の位相[rad] = 2π/365.25 × (年内日 − 81)(春分=年内日81。太陽の黄経と、赤緯の式の位相に使う)。
double SeasonalPhaseRad(int dayOfYear);

// 強制季節モード(春〜冬。RailSim II の CEnvPlugin.cpp::Render の
// abstime = 365×(mode%4)/4 − 10.0、modeは春=1〜冬=4)の絶対日。Autoでは使わない。
double ForcedAbsoluteDays(EarthRevolutionMode mode);

// 強制モードを反映した年内日。
int ResolveDayOfYear(const GameClock& clock, const SolarOverrides& overrides);

// 強制モードを反映した地方時[時](自転の強制は 12:00/0:00 に固定)。
double ResolveLocalHour(const GameClock& clock, const SolarOverrides& overrides);

// 時角[rad] = (地方時 − 12) × 15度。
double HourAngleRad(double localHour);

// 開発用の環境変数の数値(度)を読む。nullptr・空・数値でない・非有限(前後の空白は許す)は nullopt。
// 範囲のクランプは呼び出し側で行う。
std::optional<double> ParseFiniteNumber(const char* value);

// 太陽・月の高度の上書き値(度)の範囲。世界座標の北=+Z、東=+X の規約(azimuthRad は北を0として東へ増える)。
inline constexpr double kMaxElevationDeg = 90.0;
// 高度の上書き値(度)を読む。数値でなければ nullopt。範囲は±kMaxElevationDegに丸める。
std::optional<double> ParseElevationOverrideDeg(const char* value);
// 高度の上書き値(度)を、範囲に丸めてラジアンにする。
double ElevationRadFromOverride(double elevationDeg);
// 高度と方位から、方向ベクトル(北=+Z、東=+X)を求める。
void DirectionFromElevation(double elevationRad, double azimuthRad, double& x, double& y, double& z);

// 緯度・赤緯・時角から、高度・方位・方向ベクトルを求める。
HorizontalDirection EquatorialToHorizontal(double latRad, double declRad, double hourAngleRad);

} // namespace rs::world
