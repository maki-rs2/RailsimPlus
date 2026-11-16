//
// SimTuning.h - 走行システムの調整パラメータ(assets/config/sim_tuning.json)。
// 「不変の定数」ではなく実データを見ながら詰める値をここへ集める。JSONは
// CMakeのrs_embed_config_jsonでビルド時に実行ファイルへ焼き込む(配布物には置かない)。
//
#pragma once

namespace rs::sim {

// 駅舎プラグインの接近判定(_APPROACH1/_APPROACH2/_STOPPING)の先読み距離。
// RailSim II は制動距離のみを使う(CTrainGroup.cpp:908)が、制動距離を時間へ直すと
// v/(2a) となり車両の減速性能に反比例するため、警報時間が車両ごとにばらつく。
// 踏切のように一定の警報時間が要る用途に合わせ、定時間ぶんの距離との
// 大きい方を採る(制動距離を下限として残すので RailSim II より遅くはならない)。
struct ApproachTuning {
    double warningTimeS = 20.0;   // 走行速度で何秒先まで見るか
    double minDistanceM = 50.0;   // 低速・停車中でも確保する距離[m]
    double stopLimitLineM = 1.0;
};

// sim_tuning.jsonを読む。埋め込みJSONの解析に失敗しても既定値のまま使えるよう、
// 戻り値は常に構築済みの値を返す(失敗はspdlogへ警告を出す)。
struct SignalMarginsTuning {
    double movingBlockStopMarginM = 3.0;
    // ポイント在線ガードで転換できず、古い開通の先が他編成の閉塞に入るとき、分岐の
    // 手前で止まる余裕の距離[m]。
    double pointGuardStopMarginM = 3.0;
    // 信号の進入許可が入っているとき、在線または強制Rの赤の信号を通る列車にかける制限速度[km/h]。
    double entryPermitSpeedKmh = 20.0;
};

struct DeadEndStopTuning {
    double marginM = 1.0;
};

struct SeekAheadTuning {
    double marginM = 100.0;
    double minM = 500.0;
    double maxM = 2000.0;
    double stoppedVelocityEpsilonKmh = 0.1;
    double minGroupDecelerationKmhPerS = 3.0;
};

struct DiaTuning {
    double defaultDwellSeconds = 30.0;
};

// 自動停止の制動パターン(TrainKinematics.cpp DistanceToSpeed)。
struct BrakingTuning {
    // 制動を最大減速度の何割で計画するか。1.0だと離散化で速度が計画に追いきれず、
    // 停止直前に速度が残ったまま0へ切り替わる。余力を残して追従させる。
    double plannedDecelerationRatio = 0.9;
    // 減速度の変化率の上限[km/h/s^2]。ブレーキの立ち上がりと停止直前の緩めの滑らかさを決める。
    double jerkKmhPerS2 = 1.0;
    // ブレーキの立ち上がり(減速度0→計画減速度)にかかる時間の上限[s]。減速度の大きい車で
    // 立ち上がりが長くなりすぎないよう、この時間で立ち上がるジャークを下限にする。
    double maxRampTimeS = 3.0;
    // 制動中に、掛け始めの位置に来ている制約が1つも無く、残りの制約に必要な減速度が
    // 計画減速度のこの割合に満たないときはブレーキを緩めて加速へ戻す。停止点が急に遠のいた
    // (先行列車が進んだ等)ときに、遠い停止点へちょうど止まる弱いブレーキを掛け続けない
    // ため。低いほど緩めるのが遅れ、一定速度で遠ざかる先行列車への追従で速度が大きく揺れる
    // (先行60km/hに対し、0.5で50〜70km/h、0.9で59〜62km/h。最大減速度3.5km/h/sで試算)。
    double releaseDecelerationRatio = 0.9;
    // 停止制約がある間、1フレームを分ける刻みの細かさ[Hz]。大きいほどフレームレートの
    // 違いによる停止位置のずれが減るが、計算が増える。
    double substepHz = 240.0;
};

struct SimTuning {
    ApproachTuning approach;
    SignalMarginsTuning signalMargins;
    DeadEndStopTuning deadEndStop;
    SeekAheadTuning seekAhead;
    DiaTuning dia;
    BrakingTuning braking;
    // 停止位置の離散化による微小なはみ出しを在線と数えない重なり長[m]。
    double occupancyOverlapToleranceM = 0.05;

    // 埋め込みJSONから読み込む。プロセス内で1度だけ解析し、以後は同じ値を返す。
    // 他編成との接触点を先頭とみなす距離[m]。
    double occupancyNoseToleranceM = 0.05;

    static const SimTuning& Embedded();
};

} // namespace rs::sim
