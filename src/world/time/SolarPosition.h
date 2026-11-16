//
// SolarPosition.h - GameClock の時刻+緯度+地軸傾斜から太陽方向を計算する純関数。
//
// 経度・時差は扱わない(RailSim II の Latitude のみのシーン設定に合わせ、ゲーム内時刻の
// 12:00を太陽南中とする単純化モデル)。計算精度はdouble、
// 描画側(float)への変換はレンダラへ渡す境界1箇所(main.cppのSetSunDirection
// 呼び出し直前)に集約する。GUI・DirectX非依存。
//
#pragma once

#include <optional>

#include "GameClock.h"

namespace rs::world {

// RailSim II の「シミュレーション設定」ダイアログの地球の自転/公転チェック
// (CSimulationMode::m_EarthRotation/m_EarthRevolution、CEnvPlugin.cpp::Render)
// に対応する強制モード。0=Auto(実際の時刻/季節を
// 使う)、それ以外は強制表示用に固定値を使う。
// 列挙子の値は.rs2のEarthRotation/EarthRevolutionの整数値と同じ。名前の表は SolarModeNames.h。
enum class EarthRotationMode {
    Auto,
    AlwaysDay,   // 正午扱い
    AlwaysNight, // 深夜0時扱い
};

enum class EarthRevolutionMode {
    Auto,
    Spring, // 春〜冬は季節の代表日で強制
    Summer,
    Autumn,
    Winter,
};

struct SolarOverrides {
    EarthRotationMode earthRotationMode = EarthRotationMode::Auto;
    EarthRevolutionMode earthRevolutionMode = EarthRevolutionMode::Auto;
};

// 太陽位置の計算結果。
struct SolarState {
    // 太陽の方向(observerから見て太陽がある向き、正規化済み、+Yが天頂)。
    // 既存の FrameRenderer の lightDir(光が進む向き)にそのまま渡す場合は
    // 符号を反転すること(直下の Compute() コメント参照)。
    double directionToSunX = 0.0;
    double directionToSunY = 1.0;
    double directionToSunZ = 0.0;
    double elevationRad = 0.0; // 太陽高度。0未満なら太陽は地平線下(夜)
    double azimuthRad = 0.0;   // 方位角。北=0、東回り(時計回り)に増加
};

// latitudeDeg: 緯度[度](南半球は負)。axialInclinationDeg: 地軸傾斜[度]
// (既定23.44、EnvironmentPresets由来)。overrides: 自転/公転の強制モード
// (既定Auto、.rs2のEarthRotation/EarthRevolutionから設定)。
// 戻り値の directionToSun* は「観測者から見た太陽の方向」であり、
// FrameRenderer::SetSunDirection へ渡す「光が進む向き(lightDir)」には
// 符号反転(-directionToSun)が必要(main.cppの1箇所に変換を集約する)。
SolarState ComputeSolarPosition(const GameClock& clock, double latitudeDeg,
                                 double axialInclinationDeg,
                                 const SolarOverrides& overrides = SolarOverrides{});

// 太陽高度(directionToSunY、sin値)から0(完全な夜)〜1(昼)の昼夜係数を求める。
// nightThresholdSin未満で0、nightThresholdSin+0.1以上で1、その間は線形補間。
// システムスイッチ"_NIGHT"判定(PluginSwitchTable)と光源強度・空色描画の
// 両方でこの1関数に集約する(計算式変更が1箇所で済むようにする)。
double ComputeDayAlpha(double directionToSunY, double nightThresholdSin);

// 開発用: 環境変数 RS_SUN_ELEVATION_DEG(度)の値を解釈する。nullptr・空・数値でない・非有限は
// nullopt(上書きしない)。範囲外は[-90,90]へクランプする。
std::optional<double> ParseSunElevationOverrideDeg(const char* value);

// 太陽の方位は保ったまま、高度だけを elevationDeg(度、[-90,90]へクランプ)に置き換える
// (directionToSun*・elevationRadを更新)。昼夜係数・昼夜スイッチは更新後の directionToSunY から計算する。
void ApplySunElevationOverride(SolarState& state, double elevationDeg);

} // namespace rs::world
