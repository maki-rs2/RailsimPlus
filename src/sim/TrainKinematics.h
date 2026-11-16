//
// TrainKinematics.h - 編成の走行状態(速度/head前進)の tick 更新。
//
// 目標速度(targetSpeed)へ maxAcceleration/maxDeceleration でクランプ更新し、
// dt非依存積分でheadをAdvanceへ渡す（RailSim II とのビット一致は追わない）。
// targetSpeed/speedは負値も許す(負=後退方向)。前進最高速・後退最高速は
// 区別せず、maxVelocityKmhを両方向へ対称にクランプする。
// GUI・プラグイン読込非依存の純データ+ロジック（rs::world にのみ依存）。
//
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "../world/alignment/TrackTraversal.h"

namespace rs::sim {

// 編成の走行状態（tick 毎に更新される可変状態）。
struct TrainKinematics {
    // 手動の目標速度入力は、前進・後退に同じ上限[km/h]を使う。
    static constexpr float kMaxTargetSpeedKmh = 160.0f;

    rs::world::TrackPosition head; // 先頭車 FrontLimit の軌道位置
    double speed = 0.0;            // [m/s]（負=後退方向）
    double targetSpeed = 0.0;      // [m/s]（外部からのみ書き込む。負=後退方向）
    bool   atEnd = false;          // 直近tickで行き止まり/非開通に達し停止した
    // trueになるまでheadは未初期化(デフォルト構築=無効なTrackPosition)。
    // 呼び出し側は最初のTick前に実際の先頭車位置をheadへ書き込み、これをtrueにすること
    // (でなければAdvanceが存在しないedgeを参照して落ちる)。
    bool   started = false;

    // 自動停止: 目標停止地点までの残り距離[m](非負、方向はtargetSpeedの
    // 符号——0ならこれまでのspeedの符号——に従う)。設定されている間、
    // UpdateTrainSpeedはtargetSpeedそのものは書き換えず(UIのスライダー表示を
    // 保護するため)、この残り距離で止まれるように、制動パターン(計画減速度とジャーク上限
    // から止まるまでの距離を計算する。TrainKinematics.cpp DistanceToSpeed)に従って減速し、
    // ちょうど残り距離ぶん進んだところで速度を0にする。
    // nulloptなら自動停止は無効。
    // 制約: 残り距離は最初にセットした値をtickごとの走行距離(ds)で減算するだけの
    // スカラーであり、経路上のグラフ形状(分岐等)は見ていない。停止目標へ向かう
    // 途中で分岐が切り替わり実際の経路が変わっても追従しない(既知の制約)。
    std::optional<double> stopDistance;
    bool arrivedAtStop = false; // stopDistance到達により速度0で停止した(到達後はstopDistance==0のまま保持)
    // 自動停止の制動中の現在の減速度[m/s^2](非負)。減速度の変化率を抑える(ジャーク制限)ため
    // tickをまたいで持つ。制動していないときは0。UpdateTrainSpeedだけが書く。
    double brakeDecel = 0.0;
};

// 前方distanceM[m]先で、速度をlimitKmh[km/h]以下にしなければならない制約(速度制限の
// 手前の減速など)。自動停止はlimitKmh=0の制約として同じ制動パターンで扱う。
struct SpeedConstraint {
    double distanceM = 0.0;
    double limitKmh = 0.0;
};

// 停止制約の名前付き源。
// 全ての値に対して本番の書き込み元がある。
enum class StopConstraintSource {
    StationStop = 0,
    MovingBlockAhead,
    DeadEnd,
    SceneBoundary,
    PointGuard,
    Count,
};

// 制約源ごとの残り距離[m](nullopt=その源は無効)からmin()を返す。
// 全てnulloptならnullopt。配列添字順で走査するため、設定順に依存しない。
std::optional<double> MinStopConstraintM(
    const std::array<std::optional<double>, static_cast<size_t>(StopConstraintSource::Count)>&
        constraints);

// dt秒分、speedをtargetSpeedへ maxAcceleration/maxDeceleration でクランプ更新し、
// この tick で進むべき距離 ds=speed·dt[m] を返す(state.headには一切触れない)。
// maxVelocityKmh/maxAccelerationKmhPerS/maxDecelerationKmhPerS は TrainInfo 由来
// の[km/h]/[(km/h)/s]単位(内部でm/s系へ換算する)。
// speedLimits: 前方で速度を下げなければならない地点の一覧(フレームの先頭での残り距離)。
// 停止と同じ制動パターンで、手前から滑らかに減速して、その地点で制限速度以下にする。
// 呼び出し側(rs::sim::TrainRuntime::TickGroup)は返り値のdsをAdvance系関数へ渡して前進させ、
// 行き止まり等でatEndになったらspeedを0に戻す責務を別途負う(rs::world非依存の純関数)。
double UpdateTrainSpeed(double dt, double maxVelocityKmh, double maxAccelerationKmhPerS,
                          double maxDecelerationKmhPerS, TrainKinematics& state,
                          std::optional<double> externalStopConstraintM = std::nullopt,
                          const std::vector<SpeedConstraint>& speedLimits = {});

// 速度speedKmh[km/h]で走る編成が、自動停止の制動パターン(計画減速度・ジャーク上限)で
// ブレーキを掛け始めてから止まるまでに進む距離[m]。UpdateTrainSpeedがブレーキを掛け始める
// 位置と同じ計算で、駅舎の接近判定の先読み距離など、停止までの距離を知りたい側が使う。
// 減速度が0以下(減速しない車)なら0。
double PatternBrakingDistanceM(double speedKmh, double maxDecelerationKmhPerS);

} // namespace rs::sim
