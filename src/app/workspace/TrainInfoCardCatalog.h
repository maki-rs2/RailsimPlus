//
// TrainInfoCardCatalog.h - 列車モードの編成情報カードの表と速度操作の純関数
//
// 運行状態ごとの速度欄の色クラス、速度ボタン(後退・停止・前進)の目標速度、微調整の刻みと範囲を持つ。
// 種類ごとの違いは分岐でなく表で持つ。RmlUiに依存しない。
//
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>

#include "CatalogCheck.h"
#include "../../sim/DiaRuntime.h"
#include "../../sim/TrainKinematics.h"

namespace rs::app::workspace {

// 目標速度の範囲[km/h]は、走行状態を持つ型と共有する。
inline constexpr float kTrainCardMaxSpeedKmh = rs::sim::TrainKinematics::kMaxTargetSpeedKmh;
// 微調整ボタン(−/+)1回あたりの刻み[km/h]。
inline constexpr float kTrainCardSpeedStepKmh = 5.0f;
// 目標速度をこの絶対値[km/h]より小さければ「停止」の向きとみなす(スライダ操作で残る微小値を丸める)。
inline constexpr float kTrainCardStopThresholdKmh = 0.01f;

// 速度ボタンの向き。列挙順に kSpeedPresets の行が並ぶ。
enum class SpeedDirection { Reverse, Stop, Forward };

struct SpeedPreset {
    SpeedDirection direction;
    const char* key;      // RMLのイベント引数・強調の判定に使う名前
    float targetKmh;      // ボタンを押したときの目標速度
};

inline constexpr SpeedPreset kSpeedPresets[] = {
    {SpeedDirection::Reverse, "reverse", -kTrainCardMaxSpeedKmh},
    {SpeedDirection::Stop, "stop", 0.0f},
    {SpeedDirection::Forward, "forward", kTrainCardMaxSpeedKmh},
};
static_assert(std::size(kSpeedPresets) == EnumIndex(SpeedDirection::Forward) + 1,
              "kSpeedPresetsの行数がSpeedDirectionと一致しない");
static_assert(IsCatalogInEnumOrder(kSpeedPresets, &SpeedPreset::direction),
              "kSpeedPresetsの並びがSpeedDirectionの列挙順と一致しない");

constexpr const SpeedPreset& GetSpeedPreset(SpeedDirection direction) {
    return kSpeedPresets[EnumIndex(direction)];
}

// 目標速度の符号(負・0・正)に当たる向き。目標が120でも「前進」になる。
constexpr SpeedDirection DirectionOfTarget(float targetKmh) {
    if (targetKmh > kTrainCardStopThresholdKmh) return SpeedDirection::Forward;
    if (targetKmh < -kTrainCardStopThresholdKmh) return SpeedDirection::Reverse;
    return SpeedDirection::Stop;
}

// 目標速度を範囲内へ収める。
inline float ClampTargetSpeedKmh(float targetKmh) {
    return std::clamp(targetKmh, -kTrainCardMaxSpeedKmh, kTrainCardMaxSpeedKmh);
}

// 微調整ボタンをsteps回(符号つき)押した後の目標速度。
inline float StepTargetSpeedKmh(float currentKmh, int steps) {
    return ClampTargetSpeedKmh(currentKmh + kTrainCardSpeedStepKmh * static_cast<float>(steps));
}

// 運行状態(DiaState)ごとの現在速度の欄の色クラス。色の値はUIテーマ(ui_theme.json)の変数で、
// クラスとの対応はworkspace.rcssの.train-card-speed-*が持つ。
struct RunStateStyle {
    rs::sim::DiaState state;
    const char* colorClass;
};

// DiaStateの列挙順に並べる。
inline constexpr RunStateStyle kRunStateStyles[] = {
    {rs::sim::DiaState::Running, "speed-running"},     // 走行中: 通常の文字色
    {rs::sim::DiaState::Braking, "speed-braking"},     // 減速中: 橙
    {rs::sim::DiaState::Stopped, "speed-stopped"},     // 停車中: 灰
    {rs::sim::DiaState::Departing, "speed-departing"}, // 発車直前: 青緑
};
// DiaStateの値の数(最後の列挙子から求める。値が増えたらこの行の列挙子も最後のものへ直す)。
inline constexpr std::size_t kRunStateCount = EnumIndex(rs::sim::DiaState::Departing) + 1;
static_assert(std::size(kRunStateStyles) == kRunStateCount, "kRunStateStylesの行数がDiaStateと一致しない");
static_assert(IsCatalogInEnumOrder(kRunStateStyles, &RunStateStyle::state),
              "kRunStateStylesの並びがDiaStateの列挙順と一致しない");

constexpr const RunStateStyle& GetRunStateStyle(rs::sim::DiaState state) {
    return kRunStateStyles[EnumIndex(state)];
}

} // namespace rs::app::workspace
