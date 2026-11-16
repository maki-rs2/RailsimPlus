//
// TrainVehicle.h - 編成/車両の位置解決
//
// 軌道位置(TrackPosition)を起点に、車軸(Axle)をchase法で追従させ、
// 台車・車体(BodyPiece)をJointZY 2点固定の直接解法で依存して決める。
// rs::world にのみ依存する純データ+ロジック（GUI・プラグイン読込非依存）。
//
// 振り子傾斜(Tilt)はRailSim IIのソース(aizentranza/railsim2, LGPL-2.1)の
// CTrain::ApplyAxle/CBodyObject::SetPostureを移植している。キネマティクスは
// rs::sim::TrainKinematicsが別途担当する。複数車両の連結オフセットは
// rs::legacy_plugin::SolveTrainHeadsが担当する。
//
#pragma once

#include <vector>

#include "../core/Math.h"
#include "../world/alignment/TrackTraversal.h"
#include "TrainOccupiedPath.h"

namespace rs::sim {

// start から距離 distance(>=0) だけ「後方」(進行方向と逆)へ辿った軌道位置を返す。
// 戻り値の TrackPosition.dir は start.dir と同じ「車両前進方向」規約を保つ
// (Advance が内部的に使う走査向きではなく、車両が向いている方向を表す。
// Advance は ds>=0 かつ dir で走査方向を表す前提のため、後退は dir を
// 反転した TrackPosition で Advance した後、dir 表記を元の規約へ戻す)。
rs::world::AdvanceResult ChaseBackward(const rs::world::TrackGraph& graph,
                                        rs::world::TrackPosition start, double distance);

// 車軸（axle-object 相当）: レールに接地する唯一のプリミティブ。
struct Axle {
    double coordZ = 0.0;   // 車両ローカルZ（前後, front=+Z）上の車軸位置[m]
    double coordY = 0.0;   // 同Y（レール接触面=0基準, 通常=車輪半径/2）。AxleFrame の
                            // 原点を track の up 方向へこの分だけ持ち上げる。
    double diameter = 0.0; // 車輪直径[m]（0=非回転）
    int    symmetric = 1;  // N角形近似の対称数（RailSim II の Symmetric: 高速回転時の
                            // 逆回転見え(エイリアシング)防止に使う最大回転量算出用）
    bool   wheelSound = true; // この車軸がレールの継目で車輪音を鳴らすか（WheelSound）

    // 実行時（SolveAxles が書く）。
    rs::world::TrackPosition on;
    // 実行時状態（SpinAxles が書く。現在の回転位相[0,1)turn、フレームを跨いで
    // 呼び出し側が持ち込む永続値）。
    double spinFraction = 0.0;
};

// body-object（台車・車体を問わない、2点固定の剛体ピース）。
struct BodyPiece {
    // 固定先1つ分: このピースの localZY 点が、target の attachZY 点（target の
    // ローカル座標系上）とワールドで一致するよう配置する。
    struct Fix {
        enum class TargetKind { Axle, Body };
        TargetKind targetKind = TargetKind::Axle;
        int    targetIndex = 0;           // TrainVehicle::axles / bodies の添字
        double attachZ = 0, attachY = 0;  // target のローカルZY上の接続点
        double localZ = 0, localY = 0;    // 自ローカルZY上の接続点
    };
    // 規約: fix0=後方側ターゲット、fix1=前方側ターゲット（forward=normalize(P1-P0)
    // が前方を向くようにする）。
    Fix fix0, fix1;

    // 振り子傾斜（Tilt, 車体のみ opt-in）。パーツの Tilt ブロックから
    // BuildTrainVehicleTemplate が設定する静的パラメータ。
    bool   hasTilt = false;
    double tiltRatio = 0.0;     // 曲率×速度に対する振れ量係数(0.1〜10)
    double tiltMaxAngle = 0.0;  // 最大振れ角[rad]
    double tiltBaseAlt = 0.0;   // Z平行の回転軸のYローカル位置[m]

    // 実行時姿勢（SolveBodies が書く。double入力→float出力、描画境界での narrow）。
    rs::core::Vec3 posW;      // 自ローカル原点(Z=0,Y=0)のワールド位置
    rs::core::Vec3 forwardW;  // 自ローカルZ軸（正規化）
    rs::core::Vec3 upW;       // 自ローカルY軸（正規化, forwardWと直交）

    // 直近に計算した傾斜角[rad]（hasTiltのときのみ意味を持つ、参考値。RailSim IIと
    // 同じく角度そのものに1次遅れは無く、TrainVehicle::tiltDriver側の平滑化
    // ベクトルから毎回瞬時に算出される。次フレームへは持ち越さない）。
    double tiltAngle = 0.0;
};

// 振り子Tiltの駆動ベクトル永続状態（RailSim II の CTrain::m_OldPos/m_TiltDir 相当）。
// 車両(TrainVehicle)単位で1つ。フレームを跨いで呼び出し側(main.cpp等)が保持し、
// SolveBodies へ渡す。再配置(warp)時は valid=false に戻すこと（RailSim II の ResetTilt 相当。
// でないと不連続な位置差分から瞬間的な傾き(スパイク)が出る）。
struct TiltDriverState {
    rs::core::Vec3 oldPos0, oldPos1; // 前回・前々回の車両中心位置(前後軸の中点)
    rs::core::Vec3 tiltDir;          // 平滑化済み駆動ベクトル(ワールド空間)
    bool valid = false;              // false: 履歴なし。次回呼び出しで初期化のみ行う
};

// TiltDriverState を1tick分更新する（RailSim II の CTrain::ApplyAxle の該当式を移植）。
// centerPos=今回の車両中心位置(前後軸中点)。tiltSpeed[0..1]は元々RailSim IIの固定30FPS
// 前提の毎フレームブレンド率のため、dt非依存に一般化する目視合わせ係数 tiltKr を
// 介して指数ブレンドへ変換する(基準30FPS, 他の走行諸元と同じ正規化慣習)。
// 戻り値は更新後の state.tiltDir（呼び出し側の便宜用、state内の値と同一）。
rs::core::Vec3 UpdateTiltDriver(TiltDriverState& state, const rs::core::Vec3& centerPos,
                                double tiltSpeed, double tiltKr, double dt);

// 1両分（PrimaryAssembly 由来）。
struct TrainVehicle {
    double frontLimit = 0.0, tailLimit = 0.0; // 連結位置[m]（front=+Z, tail<front）
    // 編成内で前後反転。trueならSolveAxlesのchaseオフセットが
    // coordZ-tailLimit基準に切り替わり(常に軌道曲線上に乗る)、AxleFrameの
    // forwardをその場(位置不変)で180°反転し、SolveTrainVehicleが
    // SpinAxlesへ渡すdsMetersの符号も反転する(車輪回転の見た目を正しく保つ)。
    bool   flipped = false;

    // 走行諸元（TrainInfo 由来）。maxVelocity[km/h], accel/decel[(km/h)/s]、
    // tiltSpeed=振り子応答速度[0..1]。
    double maxVelocity = 0.0, maxAcceleration = 0.0, maxDeceleration = 0.0;
    double tiltSpeed = 0.0;
    // ドアを閉じ始めてから発車するまでの時間[実秒](TrainInfoのDoorClosingTime)。
    double doorClosingTime = 0.0;

    std::vector<Axle>      axles;  // Coord.Z 前後位置（>=1想定）
    // 解決順（依存順）に並んでいる前提: Fix.targetKind==Body は自分より小さい
    // index の bodies[] のみを参照できる（下位=台車を先、車体を後に置く）。
    std::vector<BodyPiece> bodies;
};

// 車両headからこの車軸までのchase距離[m]（常にheadの後方へ測る）。反転車両は前後の
// 基準が入れ替わる。SolveAxlesが車軸を置く距離と、車輪音の車軸ごとの走行距離の式が
// 同じ値を使う。
double AxleChaseOffset(const TrainVehicle& vehicle, const Axle& axle);

// head（この車両の FrontLimit の軌道位置）を起点に、全 axle の chase 位置を解決する。
void SolveAxles(const rs::world::TrackGraph& graph, const rs::world::TrackPosition& head,
                 TrainVehicle& vehicle, const TrainOccupiedPath* occupiedPath = nullptr,
                 const GraphForScene* graphForScene = nullptr,
                 double pathBaseOffsetM = 0.0);

// 車軸(Axle)の回転位相を dsMeters ぶん進める（RailSim II の CAxleObject::CalcRotation/
// CAxlePosture::Rotate 移植）。dsMeters=このtickで車両が走行した距離[m](符号付き、
// 後退ならマイナス。編成内の全axleが同じ値を共有する=RailSim IIと同じ簡略化、
// 台車ごとの実際の弧長差は無視する)。diameter=0(非回転指定)のaxleは変化しない。
// symmetric角形近似での高速回転時の見た目の逆回転(エイリアシング)を、RailSim IIの
// 定数(SYMMETRIC_ROTATION_MAX=0.45turn)を対称数で割った1tick当たりの最大回転量へ
// 指数飽和でクランプすることで防ぐ（Windmill/Tiltと同じRailSim IIの常套パターン）。
void SpinAxles(std::vector<Axle>& axles, double dsMeters);

// 解決済みの axle 位置をもとに、全 bodyPiece の姿勢を依存順(index昇順)に解決する。
// SolveAxles を先に呼んでおくこと。tiltState!=nullptr かつ hasTilt な body が
// あれば、前後軸中点の位置履歴から駆動ベクトルを更新し(UpdateTiltDriver)、
// 各bodyの(前後アンカーのright平均)への射影→指数飽和クランプで傾斜角を即座に
// 算出し姿勢へロールとして合成する（RailSim II の CBodyObject::SetPosture 相当）。
// tiltState==nullptr、またはvehicleにhasTilt bodyが無ければTilt計算自体を省略する。
// tiltKc/tiltKr は RailSim II 内部係数の一部が未取得のための目視合わせ係数。
// tiltKc=傾斜角のスケール、tiltKr=駆動ベクトルの応答速度の倍率。既定1.0。
void SolveBodies(const rs::world::TrackGraph& graph, TrainVehicle& vehicle, double dt = 0.0,
                 double tiltKc = 1.0, double tiltKr = 1.0, TiltDriverState* tiltState = nullptr);

// SolveAxles + SpinAxles + SolveBodies をまとめて行う（単一車両の位置解決の通常
// 入口）。dsMeters は車輪回転(SpinAxles)に使う（既定0.0なら回転位相は変化しない）。
void SolveTrainVehicle(const rs::world::TrackGraph& graph, const rs::world::TrackPosition& head,
                        TrainVehicle& vehicle, double dt = 0.0, double dsMeters = 0.0,
                        double tiltKc = 1.0, double tiltKr = 1.0,
                        TiltDriverState* tiltState = nullptr,
                        const TrainOccupiedPath* occupiedPath = nullptr,
                        const GraphForScene* graphForScene = nullptr,
                        double pathBaseOffsetM = 0.0);

} // namespace rs::sim
