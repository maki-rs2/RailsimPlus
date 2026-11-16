//
// MoonPosition.h - GameClock の時刻+緯度+地軸傾斜から月の方向と満ち欠けを計算する純関数。
//
// 太陽(SolarPosition)と同じ枠組み(黄道座標→赤道座標→時角)で求める。
// 計算はdouble、GUI・DirectX非依存。
//
#pragma once

#include <optional>

#include "GameClock.h"
#include "SolarPosition.h"

namespace rs::world {

// Envの Moon ブロックの値(RailSim II の全Envで 27.3 と 0.0)。
struct MoonParams {
    double revolutionPeriodDays = 27.3; // 公転周期[日](RailSim II の月の公転角の周期)
    double initialPhaseDays = 0.0;      // 初期位相[日](絶対時間に足す)
};

struct MoonState {
    // 月の方向(観測者から見て月がある向き、正規化済み、+Yが天頂、北=+Z・東=+X)。
    double directionToMoonX = 0.0;
    double directionToMoonY = 1.0;
    double directionToMoonZ = 0.0;
    double elevationRad = 0.0; // 月の高度。0未満なら地平線下
    double azimuthRad = 0.0;   // 方位角。北=0、東回りに増加
    double phaseAngleRad = 0.0; // 位相角。0=満月、π=新月
    double brightness = 0.0;    // 満月を1とする明るさの比(MoonPhaseBrightness)
};

// latitudeDeg: 緯度[度]。axialInclinationDeg: 地軸傾斜[度](黄道と赤道の傾き。太陽と同じ値)。
// overrides: 太陽と同じ自転/公転の強制モード(同じ日付・時刻で計算する)。
MoonState ComputeMoonPosition(const GameClock& clock, double latitudeDeg, double axialInclinationDeg,
                              const MoonParams& params, const SolarOverrides& overrides = SolarOverrides{});

// 位相角[rad](0=満月、π=新月)から、満月を1とする明るさの比を求める。
// 月の等級の経験式(衝効果込み)。新月側(150度超)は180度で0になるよう線形につなぐ。単調減少。
double MoonPhaseBrightness(double phaseAngleRad);

// 開発用: 環境変数 RS_MOON_ELEVATION_DEG(度)の値を解釈する。nullptr・空・数値でない・非有限は
// nullopt(上書きしない)。範囲外は[-90,90]へクランプする。
std::optional<double> ParseMoonElevationOverrideDeg(const char* value);

// 月の方位は保ったまま、高度だけを elevationDeg(度、[-90,90]へクランプ)に置き換える。
void ApplyMoonElevationOverride(MoonState& state, double elevationDeg);

// 開発用: 環境変数 RS_MOON_PHASE(位相角[度]、0=満月、180=新月)の値を解釈する。
// 不正値は nullopt。範囲外は[0,180]へクランプする。
std::optional<double> ParseMoonPhaseOverrideDeg(const char* value);

// 位相角を phaseDeg(度、[0,180]へクランプ)に置き換え、明るさの比もそれに合わせて求め直す。
void ApplyMoonPhaseOverride(MoonState& state, double phaseDeg);

} // namespace rs::world
