#include "TrainVehicle.h"

#include "../core/Epsilon.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rs::sim {

using rs::core::Vec3;
using rs::world::AdvanceResult;
using rs::world::TrackEdge;
using rs::world::TrackFrame;
using rs::world::TrackGraph;
using rs::world::TrackPosition;

AdvanceResult ChaseBackward(const TrackGraph& graph, TrackPosition start, double distance) {
    TrackPosition flipped = start;
    flipped.direction = -flipped.direction;
    AdvanceResult r = rs::world::Advance(graph, flipped, distance);
    r.pos.direction = -r.pos.direction;
    return r;
}

namespace {

// target(Axle または既に解決済みの下位BodyPiece)のワールド空間フレーム。
struct WorldFrame {
    Vec3 origin, forward, up;
};

// flipped=true: 編成内でこの車両が前後反転していることを表す。ワールド位置
// (originおよびSolveAxles側のchaseオフセット)は「軌道上の実際の点」を指し
// 続けるため変更しない。変えるのはローカル前方軸(forward)の符号のみ:
// forward=-f.tangent とすることで、そのanchor点を「その場で(位置は動かさず)
// 鉛直軸まわりに180°回転させた」姿勢になる(「軸オフセット式の直接鏡像+axle.on.dir反転」は
// SolveBodiesの依存chain全体を歪めるため使わない。axle.on.dir自体は「軌道走査上の進行方向」規約を保つ
// 必要があり反転しないこと)。
WorldFrame AxleFrame(const TrackGraph& graph, const Axle& axle, bool flipped) {
    const TrackEdge& edge = graph.edges.at(axle.on.edge);
    TrackFrame f = rs::world::EvaluateAt(edge, axle.on.s, axle.on.direction);
    // 車軸中心はレール接触面から up 方向へ coordY(通常=車輪半径/2)だけ上にある。
    // レール接触面はedge.railTopLift(Rail/Tie積み上げによる地面からの持ち上げ量)
    // だけspline(地面基準)より上にある。
    const Vec3 origin = f.pos + f.up * static_cast<float>(axle.coordY + edge.railTopLift);
    const Vec3 forward = flipped ? f.tangent * -1.0f : f.tangent;
    return WorldFrame{origin, forward, f.up};
}

WorldFrame BodyFrame(const BodyPiece& body) {
    return WorldFrame{body.posW, body.forwardW, body.upW};
}

WorldFrame ResolveTarget(const TrackGraph& graph, const TrainVehicle& vehicle,
                          const BodyPiece::Fix& fix) {
    if (fix.targetKind == BodyPiece::Fix::TargetKind::Axle) {
        return AxleFrame(graph, vehicle.axles[fix.targetIndex], vehicle.flipped);
    }
    return BodyFrame(vehicle.bodies[fix.targetIndex]);
}

Vec3 AttachPointWorld(const WorldFrame& target, double attachZ, double attachY) {
    return target.origin + target.forward * static_cast<float>(attachZ) +
           target.up * static_cast<float>(attachY);
}

// v を axis(単位ベクトル)まわりへ angleRad 回転する。v⊥axis 前提
// （Rodrigues の回転公式の投影項 axis*(axis・v) が 0 になる場合の簡約形）。
Vec3 RotateAroundAxis(const Vec3& v, const Vec3& axis, double angleRad) {
    const float c = static_cast<float>(std::cos(angleRad));
    const float s = static_cast<float>(std::sin(angleRad));
    return v * c + Vec3::Cross(axis, v) * s;
}

// RailSim IIの「MaxAngleへ指数飽和で漸近するクランプ」(Windmillの回転量制限や
// Tilt角クランプで共通して使われるパターン)。x を ±max の範囲へ滑らかに丸める。
double SoftClamp(double x, double max) {
    if (max <= rs::core::kSoftClampScaleEps) return 0.0;
    const double magnitude = max * (1.0 - std::exp(-std::fabs(x) / max));
    return x < 0.0 ? -magnitude : magnitude;
}

} // namespace

double AxleChaseOffset(const TrainVehicle& vehicle, const Axle& axle) {
    return vehicle.flipped ? (axle.coordZ - vehicle.tailLimit)
                           : (vehicle.frontLimit - axle.coordZ);
}

void SolveAxles(const TrackGraph& graph, const TrackPosition& head, TrainVehicle& vehicle,
                const TrainOccupiedPath* occupiedPath, const GraphForScene* graphForScene,
                double pathBaseOffsetM) {
    for (Axle& axle : vehicle.axles) {
        // flipped=true: head は「反転前ならFrontLimitが来る軌道位置」のまま
        // (呼び出し側=SolveTrainHeadsは反転を考慮せず車両長だけで間隔を
        // 決める)。反転車両は物理的に前後が入れ替わるため、
        // coordZ=tailLimit(物理的な後尾)がheadへ来る=offset 0、
        // coordZ=frontLimit(物理的な先頭)がheadからlengthだけ後方、という
        // 対応になる。offsetの符号だけが変わり、chase自体は常にhead.dirへ
        // 沿って行われる(=各軸は必ず軌道曲線上に乗る。axle.on.dirまで反転すると
        // SolveBodiesを歪ませる)。
        const double offset = AxleChaseOffset(vehicle, axle);
        // 占有edge列は編成先頭head起点で距離を測るため、この車両headまでの
        // 弧長(pathBaseOffsetM)を足してから引く。
        size_t pathScene = 0;
        TrackPosition pathPos;
        if (occupiedPath != nullptr && graphForScene != nullptr &&
            ChaseBackwardOnPath(*occupiedPath, *graphForScene, pathBaseOffsetM + offset, pathScene,
                                pathPos)) {
            axle.on = pathPos;
        } else {
            axle.on = ChaseBackward(graph, head, offset).pos;
        }
    }
}

namespace {
// RailSim II の SYMMETRIC_ROTATION_MAX(CNamedObject.h)。N角形近似の対称数で割った値が
// 「1tick当たりの最大回転量[turn]」となり、これで車輪回転の増分を指数飽和させる
// ことで高速回転時の見た目の逆回転(エイリアシング)を防ぐ（Windmill/Tiltと同じ
// RailSim IIの常套パターン）。
constexpr double kSymmetricRotationMax = 0.45;
} // namespace

void SpinAxles(std::vector<Axle>& axles, double dsMeters) {
    for (Axle& axle : axles) {
        if (axle.diameter <= 0.0) continue; // 非回転指定
        const int symmetric = std::max(1, axle.symmetric);
        const double maxRotation = kSymmetricRotationMax / symmetric; // [turn/tick]
        // RailSim II の CAxleObject::CalcRotation を移植:
        //   tmp = maxRotation*(1-exp(-|dist|/(maxRotation*Diameter*π)))
        const double denom = maxRotation * axle.diameter * std::numbers::pi;
        if (denom <= rs::core::kAxleRotationDistanceEpsM) continue;
        const double magnitude = maxRotation * (1.0 - std::exp(-std::fabs(dsMeters) / denom));
        axle.spinFraction += dsMeters < 0.0 ? -magnitude : magnitude;
        axle.spinFraction -= std::floor(axle.spinFraction); // [0,1)turnへラップ
    }
}

Vec3 UpdateTiltDriver(TiltDriverState& state, const Vec3& centerPos, double tiltSpeed,
                      double tiltKr, double dt) {
    if (!state.valid) {
        // RailSim II の ResetTilt 相当: 履歴が無い(初回/再配置直後)ときは駆動ベクトル
        // 0・履歴=現在位置で初期化するのみ。ここで raw を計算すると位置の不連続
        // (再配置によるジャンプ)がそのまま傾きスパイクになってしまう。
        state.oldPos0 = state.oldPos1 = centerPos;
        state.tiltDir = Vec3(0, 0, 0);
        state.valid = true;
        return state.tiltDir;
    }
    // RailSim II の CTrain::ApplyAxle の式をそのまま移植（P(t)+P(t-1)-2*P(t-2) の形。
    // 教科書的な対称2階差分(P(t)-2P(t-1)+P(t-2))ではないが、この非対称バイアスは
    // 常に進行方向寄りに乗るため、後段でbodyのright軸へ射影した時点でほぼ相殺
    // される。"修正"せず原典どおり移植する）。
    const Vec3 raw = (state.oldPos0 - state.oldPos1) - (state.oldPos1 - centerPos);
    // tiltSpeedはRailSim IIでは固定30FPS想定の毎フレームブレンド率(0..1)。可変dtの
    // このエンジンでは指数ブレンドへ変換する(基準30FPS, 他の走行諸元と同じ正規化
    // 慣習）。tiltKrは目視合わせ用の倍率(既定1.0)。
    const double blend = std::clamp(tiltSpeed * tiltKr, 0.0, 1.0);
    const double alpha = 1.0 - std::pow(1.0 - blend, dt * 30.0);
    state.tiltDir = state.tiltDir * static_cast<float>(1.0 - alpha) + raw * static_cast<float>(alpha);
    state.oldPos1 = state.oldPos0;
    state.oldPos0 = centerPos;
    return state.tiltDir;
}

void SolveBodies(const TrackGraph& graph, TrainVehicle& vehicle, double dt, double tiltKc,
                 double tiltKr, TiltDriverState* tiltState) {
    // 振り子Tilt駆動ベクトルの更新(車両1つにつき1回。RailSim II の CTrain::ApplyAxle
    // 相当)。対象bodyが無ければ計算自体を省く。
    Vec3 tiltDir(0, 0, 0);
    if (tiltState && !vehicle.axles.empty()) {
        const bool anyTilt = std::any_of(vehicle.bodies.begin(), vehicle.bodies.end(),
                                         [](const BodyPiece& b) { return b.hasTilt; });
        if (anyTilt) {
            const Vec3 frontPos = AxleFrame(graph, vehicle.axles.front(), vehicle.flipped).origin;
            const Vec3 rearPos = AxleFrame(graph, vehicle.axles.back(), vehicle.flipped).origin;
            const Vec3 centerPos = (frontPos + rearPos) * 0.5f;
            tiltDir = UpdateTiltDriver(*tiltState, centerPos, vehicle.tiltSpeed, tiltKr, dt);
        }
    }

    for (BodyPiece& body : vehicle.bodies) {
        const WorldFrame t0 = ResolveTarget(graph, vehicle, body.fix0);
        const WorldFrame t1 = ResolveTarget(graph, vehicle, body.fix1);
        const Vec3 p0 = AttachPointWorld(t0, body.fix0.attachZ, body.fix0.attachY);
        const Vec3 p1 = AttachPointWorld(t1, body.fix1.attachZ, body.fix1.attachY);

        const Vec3 forward = (p1 - p0).Normalized();
        const Vec3 upAvg = t0.up + t1.up;
        Vec3 up = (upAvg - forward * Vec3::Dot(upAvg, forward)).Normalized();

        // RailSim II の CBodyObject::SetPosture (CTrainPlugin.cpp) 移植:
        // cpos=0.5*(j1+j2), dif=0.5*(local1+local2) の「2アンカー平均」で原点を
        // 求める（片方だけを基準にすると、車軸間隔とLocalCoord間隔が厳密一致
        // しないデータで
        // 誤差が全量そちらへ乗ってしまう。RailSim II は誤差を両アンカーへ半分ずつ
        // 分配する）。cpos-dif.z*f-dif.y*u は代数的に
        // 0.5*[(p0-local0.z*f-local0.y*u)+(p1-local1.z*f-local1.y*u)] と等価
        // なので、既存の片側算出式を両アンカーぶん計算して平均するだけで済む
        // (forward/up の算出方法自体は変更しない)。
        const Vec3 origin0 = p0 - (forward * static_cast<float>(body.fix0.localZ) +
                                    up * static_cast<float>(body.fix0.localY));
        const Vec3 origin1 = p1 - (forward * static_cast<float>(body.fix1.localZ) +
                                    up * static_cast<float>(body.fix1.localY));
        Vec3 origin = (origin0 + origin1) * 0.5f;

        if (body.hasTilt && tiltState) {
            // RailSim II の CBodyObject::SetPosture 相当: 2つのアンカー(通常=前後台車)
            // それぞれの right(=up×forward相当)を合算した軸へ駆動ベクトルを射影
            // し、MaxAngleへ指数飽和クランプする。角度自体に別途1次遅れは無い
            // (応答速度は駆動ベクトル側のUpdateTiltDriverで既に効いている)。
            const Vec3 t0Right = Vec3::Cross(t0.up, t0.forward);
            const Vec3 t1Right = Vec3::Cross(t1.up, t1.forward);
            const Vec3 rightAvg = (t0Right + t1Right).Normalized();
            const double rawTilt = body.tiltRatio * static_cast<double>(Vec3::Dot(tiltDir, rightAvg)) * tiltKc;
            body.tiltAngle = SoftClamp(rawTilt, body.tiltMaxAngle);

            // BaseAlt(ローカルY)を通る forward 平行軸まわりのロールとして合成。
            const Vec3 pivot = origin + up * static_cast<float>(body.tiltBaseAlt);
            const Vec3 newUp = RotateAroundAxis(up, forward, body.tiltAngle);
            origin = pivot - newUp * static_cast<float>(body.tiltBaseAlt);
            up = newUp;
        }

        body.posW = origin;
        body.forwardW = forward;
        body.upW = up;
    }
}

void SolveTrainVehicle(const TrackGraph& graph, const TrackPosition& head, TrainVehicle& vehicle,
                       double dt, double dsMeters, double tiltKc, double tiltKr,
                       TiltDriverState* tiltState, const TrainOccupiedPath* occupiedPath,
                       const GraphForScene* graphForScene, double pathBaseOffsetM) {
    SolveAxles(graph, head, vehicle, occupiedPath, graphForScene, pathBaseOffsetM);
    // flipped車両はローカルforward(=right軸も連動)が実世界に対して反転している
    // (AxleFrame参照)。同じ実世界の転がり方向を保つには、ローカルX(=right)軸
    // まわりの回転角の符号も反転させる必要がある(でないと反転車両の
    // 車軸回転が逆に見える)。
    SpinAxles(vehicle.axles, vehicle.flipped ? -dsMeters : dsMeters);
    SolveBodies(graph, vehicle, dt, tiltKc, tiltKr, tiltState);
}

} // namespace rs::sim
