#include "TrainKinematics.h"

#include "../core/Epsilon.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "../core/SpeedUnits.h"
#include "SimTuning.h"

namespace rs::sim {

namespace {

// 自動停止の制動パターン。減速度が一定のジャークで立ち上がり、計画減速度を保ち、
// 停止の直前に一定のジャークで0へ絞られる形で止まる。止まるまでに進む距離を、車両の
// 特性(最大減速度・ジャーク)と今の速度・減速度から直接計算して、ブレーキを掛け始める
// 位置と、その後の減速度を決める。
struct BrakingParams {
    double maxDecel;      // 最大減速度[m/s^2]
    double plannedDecel;  // 計画減速度[m/s^2]
    double jerk;          // 減速度の変化率の上限[m/s^3]
    double releaseDecel;  // 掛け始めの位置に来た制約が無いとき、これ未満なら緩める減速度[m/s^2]
};

// 減速度の下限(0を避けるための小さな値)と、制動中とみなす減速度のしきい値[m/s^2]。
constexpr double kMinPatternDecel = 1e-6;
constexpr double kBrakingDecelEpsilon = 1e-9;
// 目標の減速度を二分探索する回数と、探索する減速度の下限[m/s^2]。
constexpr int kPatternSearchIterations = 40;
// 1フレームを刻みに分ける数の上限。フレームが極端に長いとき、刻みを細かくしすぎない。
constexpr int kMaxSubsteps = 64;
constexpr double kPatternSearchMinDecel = 1e-4;

BrakingParams ResolveBrakingParams(double maxDecelerationKmhPerS) {
    const BrakingTuning& tuning = SimTuning::Embedded().braking;
    BrakingParams params;
    params.maxDecel = maxDecelerationKmhPerS * rs::core::kKilometersPerHourToMetersPerSecond;
    params.plannedDecel = params.maxDecel * tuning.plannedDecelerationRatio;
    // 減速度が大きい車でも立ち上がりがmaxRampTimeSを超えないよう、ジャークの下限を決める。
    params.jerk = std::max(tuning.jerkKmhPerS2 * rs::core::kKilometersPerHourToMetersPerSecond,
                           params.plannedDecel / tuning.maxRampTimeS);
    params.releaseDecel = params.plannedDecel * tuning.releaseDecelerationRatio;
    return params;
}

// 落とす速度excess[m/s]を、減速度decelから一定のジャークjerkでpeakまで変え→peakを保ち→0へ
// 一定のジャークjerkで絞る、という形で落としきるまでに進む距離[m]と時間[s]。excessは、停止なら
// 今の速度、目標速度つきなら今の速度-目標速度。peakは、絞りまで収まる範囲
// (sqrt(j·excess + decel^2/2))に抑える。
struct BrakingProfile {
    double distance;
    double time;
};

BrakingProfile ShedSpeedProfile(double excess, double decel, double peak, double jerk) {
    peak = std::max(std::min(peak, std::sqrt(jerk * excess + decel * decel / 2.0)),
                    kMinPatternDecel);
    const double sign = (peak >= decel) ? 1.0 : -1.0;
    const double rampTime = std::abs(peak - decel) / jerk;
    // 区間1: 減速度decelからpeakまで。decel(t)=decel+sign·jerk·t。
    const double speedAfterRamp =
        excess - decel * rampTime - sign * jerk * rampTime * rampTime / 2.0;
    if (speedAfterRamp <= 0.0) {
        // 区間1の途中で落としきる。excess - decel·t - sign·jerk·t^2/2 = 0 を解く。
        const double a = sign * jerk / 2.0;
        const double t = (std::abs(a) < rs::core::kBrakingQuadraticCoefficientEps)
            ? excess / std::max(decel, kBrakingDecelEpsilon)
            : (-decel + std::sqrt(std::max(0.0, decel * decel + 2.0 * sign * jerk * excess))) /
                  (2.0 * a);
        return {excess * t - decel * t * t / 2.0 - sign * jerk * t * t * t / 6.0, t};
    }
    const double rampDistance = excess * rampTime - decel * rampTime * rampTime / 2.0 -
                                sign * jerk * rampTime * rampTime * rampTime / 6.0;
    // 区間3(絞り)で失う速度。これだけ残して区間2(peakを保つ)を終える。
    const double taperSpeed = peak * peak / (2.0 * jerk);
    if (speedAfterRamp < taperSpeed) {
        // 絞りの途中で落としきる。
        const double t =
            (peak - std::sqrt(std::max(0.0, peak * peak - 2.0 * jerk * speedAfterRamp))) / jerk;
        return {rampDistance + speedAfterRamp * t - peak * t * t / 2.0 + jerk * t * t * t / 6.0,
                rampTime + t};
    }
    const double holdTime = (speedAfterRamp - taperSpeed) / peak;
    const double holdDistance =
        (speedAfterRamp * speedAfterRamp - taperSpeed * taperSpeed) / (2.0 * peak);
    return {rampDistance + holdDistance + peak * peak * peak / (6.0 * jerk * jerk),
            rampTime + holdTime + peak / jerk};
}

// 速度speedから目標速度targetまで落とす間に進む距離[m]。落とす間は目標速度でも進み続ける
// ので、落とす速度の制動パターンが進む距離に、目標速度×かかる時間を足す(停止ならtarget=0)。
// speedがtarget以下なら0。
double DistanceToSpeed(double speed, double target, double decel, double peak, double jerk) {
    const double excess = speed - target;
    if (excess <= 0.0) return 0.0;
    const BrakingProfile profile = ShedSpeedProfile(excess, decel, peak, jerk);
    return target * profile.time + profile.distance;
}

struct BrakingCommand {
    double decel;     // 目標の減速度[m/s^2]
    bool emergency;   // 最大減速度でも間に合わない(ジャークの制限を外す)
};

// 残り距離remainingで目標速度targetまで落とす目標の減速度を求める(掛け始めるかどうかの
// 判定は含まない)。
BrakingCommand SolveBrakingTargetFor(double speed, double decel, double remaining, double target,
                                     const BrakingParams& params) {
    const bool emergency = speed > std::sqrt(target * target + 2.0 * params.maxDecel * remaining);
    if (DistanceToSpeed(speed, target, decel, params.maxDecel, params.jerk) > remaining) {
        return {params.maxDecel, emergency};
    }
    double low = kPatternSearchMinDecel;
    double high = params.maxDecel;
    if (DistanceToSpeed(speed, target, decel, low, params.jerk) < remaining) return {low, emergency};
    for (int i = 0; i < kPatternSearchIterations; ++i) {
        const double mid = (low + high) / 2.0;
        if (DistanceToSpeed(speed, target, decel, mid, params.jerk) > remaining) low = mid;
        else high = mid;
    }
    return {(low + high) / 2.0, emergency};
}

// 停止制約(残り距離stopRemaining、目標速度0)と速度制限の制約limitsのうち、いちばん厳しい
// ものに合わせた目標の減速度を求める。計画減速度aPで今から掛け始めると間に合わなくなる
// 制約(掛け始めの位置に来た制約)だけを対象にする。どれも掛け始めの位置に来ていないのに
// 制動中なら(計画より先に進みすぎて緩めている間)、すべての制約から求める。そのとき要る
// 減速度がreleaseDecel未満なら(停止点が遠のいた)、ブレーキを緩めて加速へ戻す。
BrakingCommand SolveBrakingTarget(double speed, double decel, std::optional<double> stopRemaining,
                                  const std::vector<SpeedConstraint>& limits,
                                  const BrakingParams& params) {
    const bool braking = decel > kBrakingDecelEpsilon;
    BrakingCommand best{0.0, false};
    bool anyAtOnset = false;
    const auto consider = [&](double remaining, double target, bool onlyAtOnset) {
        if (speed <= target) return;
        const bool atOnset =
            DistanceToSpeed(speed, target, 0.0, params.plannedDecel, params.jerk) >= remaining;
        if (atOnset) anyAtOnset = true;
        if (!atOnset && onlyAtOnset) return;
        const BrakingCommand command = SolveBrakingTargetFor(speed, decel, remaining, target, params);
        if (command.decel > best.decel) best = command;
    };
    const auto considerAll = [&](bool onlyAtOnset) {
        if (stopRemaining.has_value()) consider(*stopRemaining, 0.0, onlyAtOnset);
        for (const SpeedConstraint& limit : limits) {
            //地点に着いた制約は手前ではないので対象外。以降は区間の上限が速度を抑える
            if (limit.distanceM <= 0.0) continue;
            consider(limit.distanceM, limit.limitKmh * rs::core::kKilometersPerHourToMetersPerSecond, onlyAtOnset);
        }
    };
    considerAll(true);
    if (!anyAtOnset && braking) {
        considerAll(false);
        if (best.decel < params.releaseDecel) best = BrakingCommand{0.0, false};
    }
    return best;
}

// 制動中の減速度を1tick進める。目標の減速度へ、変化率をジャーク以下に抑えて近づける。
// 最大減速度でも間に合わない速度のときだけ、ジャークの制限を外す(信号が急に赤に
// なったときなどの非常ブレーキ)。
double StepBrakingDecel(double speedAlong, std::optional<double> stopRemaining,
                        const std::vector<SpeedConstraint>& limits, double dt,
                        const BrakingParams& params, double prevDecel) {
    const BrakingCommand command =
        SolveBrakingTarget(speedAlong, prevDecel, stopRemaining, limits, params);
    if (command.emergency) return command.decel;
    return std::clamp(command.decel, prevDecel - params.jerk * dt, prevDecel + params.jerk * dt);
}

// speedを、符号つきのtargetへ加速度aAccel/減速度aDecelで近づける。
double ApproachTargetSpeed(double speed, double target, double aAccel, double aDecel, double dt) {
    const double diff = target - speed;
    if (diff > 0.0) {
        const double rate = (speed >= 0.0) ? aAccel : aDecel;
        return std::min(target, speed + rate * dt);
    }
    if (diff < 0.0) {
        const double rate = (speed <= 0.0) ? aAccel : aDecel;
        return std::max(target, speed - rate * dt);
    }
    return speed;
}
} // namespace

double PatternBrakingDistanceM(double speedKmh, double maxDecelerationKmhPerS) {
    if (maxDecelerationKmhPerS <= 0.0) return 0.0;
    const BrakingParams params = ResolveBrakingParams(maxDecelerationKmhPerS);
    return DistanceToSpeed(std::abs(speedKmh) * rs::core::kKilometersPerHourToMetersPerSecond, 0.0, 0.0, params.plannedDecel, params.jerk);
}

std::optional<double> MinStopConstraintM(
    const std::array<std::optional<double>, static_cast<size_t>(StopConstraintSource::Count)>&
        constraints) {
    std::optional<double> best;
    for (const std::optional<double>& value : constraints) {
        if (!value.has_value()) continue;
        if (!best.has_value() || *value < *best) best = value;
    }
    return best;
}

namespace {

// UpdateTrainSpeedの1刻み分。dtは1回の更新の長さ(刻みに分けたあとの値)。
double StepTrainSpeed(double dt, double maxVelocityKmh, double maxAccelerationKmhPerS,
                      double maxDecelerationKmhPerS, TrainKinematics& state,
                      std::optional<double> externalStopConstraintM,
                      const std::vector<SpeedConstraint>& speedLimits) {
    const double vMax = maxVelocityKmh * rs::core::kKilometersPerHourToMetersPerSecond;
    const double aAccel = maxAccelerationKmhPerS * rs::core::kKilometersPerHourToMetersPerSecond;
    const double aDecel = maxDecelerationKmhPerS * rs::core::kKilometersPerHourToMetersPerSecond;
    // targetSpeedは後退方向なら負値。vMaxは向きを問わず対称にクランプする
    // (前進最高速と後退最高速を同じ値として扱う。RailSim II 側にも別値の概念は無い)。
    double target = std::clamp(state.targetSpeed, -vMax, vMax);

    // 自動停止: stopDistanceが設定されていれば、制動パターン(DistanceToSpeed)で
    // 止まる位置から減速する。targetSpeed自体(ユーザー設定値)は書き換えない(UIスライダーの
    // 表示保護のため)。
    // StationStopの帳簿と外部供給分を合成する。帳簿の減算とarrivedAtStopは
    // StationStopだけが管理し、外部源は毎tick再供給される。
    std::optional<double> effectiveStopDistance;
    bool stationStopIsBinding = false;
    if (state.stopDistance.has_value() || externalStopConstraintM.has_value()) {
        const double stationStopDistance =
            state.stopDistance.value_or(std::numeric_limits<double>::infinity());
        const double externalStopDistance =
            externalStopConstraintM.value_or(std::numeric_limits<double>::infinity());
        effectiveStopDistance = std::min(stationStopDistance, externalStopDistance);
        stationStopIsBinding = stationStopDistance <= externalStopDistance;
    }

    // 自動停止と速度制限の手前の減速は、同じ制動パターン(計画減速度=最大減速度の
    // plannedDecelerationRatio倍、ジャークの上限つき)で進める(StepBrakingDecel)。最大減速度ちょうどで計画すると、
    // 離散化で速度が計画に追いきれず、停止直前に速度が残ったまま0へ切り替わる。
    // 制動中でない間は、1tickで進む距離が残り距離を超えない速度までで加速する。
    // 0から離れる(前進中に加速/後退中に加速)ならaAccel、0へ近づく(いずれの方向の
    // ブレーキも含む)ならaDecelを使う。符号が反転する目標(前進中に後退目標、
    // 逆も同様)は、まず0へ向けて減速し、0を過ぎた次tick以降で自動的にaAccel側へ
    // 切り替わる(速度0を跨ぐ瞬間の1tickぶんだけ理想よりわずかに減速側に長く
    // 倒れるが、dt刻みでの近似として無視できる。RailSim IIとのビット一致は追わない方針)。
    const double direction = (target != 0.0) ? (target > 0.0 ? 1.0 : -1.0)
                                              : (state.speed >= 0.0 ? 1.0 : -1.0);
    bool braking = false;
    double userSpeed = state.speed;  // 制動中にユーザーが目標を下げたときの減速(ジャーク制限なし)
    double brakedSpeed = state.speed;
    double clampedTarget = target;
    if ((!effectiveStopDistance.has_value() && speedLimits.empty()) || aDecel <= 0.0) {
        state.brakeDecel = 0.0;
    } else {
        const BrakingParams params = ResolveBrakingParams(maxDecelerationKmhPerS);
        const double speedAlong = direction * state.speed;
        std::optional<double> stopRemaining;
        if (effectiveStopDistance.has_value()) {
            stopRemaining = std::max(0.0, *effectiveStopDistance);
            clampedTarget = direction * std::min(std::abs(target), *stopRemaining / dt);
        }
        if (speedAlong > 0.0) {
            const double decel = StepBrakingDecel(speedAlong, stopRemaining, speedLimits, dt,
                                                  params, state.brakeDecel);
            braking = decel > 0.0 || state.brakeDecel > 0.0;
            state.brakeDecel = decel;
            brakedSpeed = direction * std::max(0.0, speedAlong - decel * dt);
            userSpeed = ApproachTargetSpeed(state.speed, target, aAccel, aDecel, dt);
        } else {
            state.brakeDecel = 0.0;
        }
    }

    if (braking) {
        state.speed = direction * std::min(direction * brakedSpeed, direction * userSpeed);
    } else {
        state.speed = ApproachTargetSpeed(state.speed, clampedTarget, aAccel, aDecel, dt);
    }
    state.speed = std::clamp(state.speed, -vMax, vMax);

    double ds = state.speed * dt;

    // 残り距離をちょうど使い切ったところで確実に止める(行き止まり停止と同じ
    // 「境界でクランプする」方針。離散dt積分でオーバーシュートすることがあるため)。
    // 制動で速度がちょうど0になった地点が停止余裕(RailSim II のSTOP_LIMIT_LINE)以内なら、その場で
    // 到着とする。位置は動かさない(残りの誤差は許容する)。扱わないと、残りの数cmのために
    // 再加速して制動を繰り返す。
    if (effectiveStopDistance.has_value()) {
        const double remaining = *effectiveStopDistance;
        const bool stoppedShort = braking && state.speed == 0.0 &&
                                  remaining <= SimTuning::Embedded().approach.stopLimitLineM;
        if (stoppedShort || std::abs(ds) >= remaining) {
            ds = stoppedShort ? 0.0 : direction * remaining;
            state.speed = 0.0;
            state.brakeDecel = 0.0;
            if (stationStopIsBinding) {
                state.stopDistance = 0.0;
                state.arrivedAtStop = true;
            } else if (state.stopDistance.has_value()) {
                state.stopDistance = std::max(0.0, *state.stopDistance - std::abs(ds));
            }
        } else if (state.stopDistance.has_value()) {
            state.stopDistance = *state.stopDistance - std::abs(ds);
        }
    }

    return ds;
}

// このフレームを刻みに分けて進める必要があるか。停止制約がある間と制動中は必要。速度制限の
// 制約は、このフレームのうちに制動を掛け始める位置に来るときだけ必要(それまでは分けても
// 結果が変わらず、負荷だけ増える)。
bool NeedsSubstepping(double dt, double maxDecelerationKmhPerS, const TrainKinematics& state,
                      bool hasStopConstraint, const std::vector<SpeedConstraint>& limits) {
    if (hasStopConstraint || state.brakeDecel > kBrakingDecelEpsilon) return true;
    if (limits.empty() || maxDecelerationKmhPerS <= 0.0) return false;
    const BrakingParams params = ResolveBrakingParams(maxDecelerationKmhPerS);
    const double speed = std::abs(state.speed);
    for (const SpeedConstraint& limit : limits) {
        const double target = limit.limitKmh * rs::core::kKilometersPerHourToMetersPerSecond;
        if (speed <= target) continue;
        const double onsetDistance =
            DistanceToSpeed(speed, target, 0.0, params.plannedDecel, params.jerk);
        if (limit.distanceM <= onsetDistance + 2.0 * speed * dt) return true;
    }
    return false;
}

} // namespace

double UpdateTrainSpeed(double dt, double maxVelocityKmh, double maxAccelerationKmhPerS,
                          double maxDecelerationKmhPerS, TrainKinematics& state,
                          std::optional<double> externalStopConstraintM,
                          const std::vector<SpeedConstraint>& speedLimits) {
    // 停止制約がある間は、1フレームを細かい刻みに分けて進める。制動の開始位置と最後の停止が
    // フレームの長さで決まってしまい、粗いフレームほど停止位置がずれるため。
    // 制約がなければ(速度の更新は線形なので)分けても結果が変わらず、負荷だけ増えるので1回で進める。
    const bool hasStopConstraint =
        state.stopDistance.has_value() || externalStopConstraintM.has_value();
    int substeps = 1;
    if (dt > 0.0 &&
        NeedsSubstepping(dt, maxDecelerationKmhPerS, state, hasStopConstraint, speedLimits)) {
        const double substepS = 1.0 / SimTuning::Embedded().braking.substepHz;
        substeps = std::clamp(static_cast<int>(std::ceil(dt / substepS - rs::core::kSubstepCountEps)), 1, kMaxSubsteps);
    }
    const double stepDt = dt / substeps;
    std::vector<SpeedConstraint> limits = speedLimits;  // 刻みごとに進んだぶんを引く
    double dsTotal = 0.0;
    for (int i = 0; i < substeps; ++i) {
        const double ds = StepTrainSpeed(stepDt, maxVelocityKmh, maxAccelerationKmhPerS,
                                         maxDecelerationKmhPerS, state, externalStopConstraintM,
                                         limits);
        dsTotal += ds;
        // 外から渡された制約は、フレームの先頭での残り距離。進んだぶんを減らす
        // (stopDistanceはStepTrainSpeedが自分で減らす)。
        if (externalStopConstraintM.has_value()) {
            externalStopConstraintM = std::max(0.0, *externalStopConstraintM - std::abs(ds));
        }
        for (SpeedConstraint& limit : limits) {
            limit.distanceM = std::max(0.0, limit.distanceM - std::abs(ds));
        }
    }
    return dsTotal;
}

} // namespace rs::sim
