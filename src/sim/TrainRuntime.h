//
// TrainRuntime.h - 編成(trainId)ごとの走行状態(TrainKinematics)を
// 保持・駆動するGPU非依存の実行時状態。
//
// 走行状態は描画クラス(rs::app::SceneRenderer)ではなくこのクラスが持つ
// (rs::sim層、rs::world以外へ依存しない)。SceneRendererはこのクラスのインスタンスをメンバとして持ち、
// 速度設定/取得・削除の委譲、およびTickGroupで各tickのhead前進量を得る。
//
// 各trainは「自分が今どの
// Scene(sceneIndex)で走っているか」を保持する。Scene切替(graph丸ごと差替え)は
// 「今どのSceneが画面に表示されているか」の話でしかなく、trainは自分の
// sceneIndexが指すgraphに対してtickし続けるため、Scene切替のたびにGuidが
// ダングリングになる問題が構造的に起きなくなる。
//
#pragma once

#include <cstddef>
#include <array>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../world/alignment/TrackForwardScan.h"
#include "../world/alignment/PointRoutineResolve.h"
#include "../world/alignment/TrackTraversal.h"
#include "../world/alignment/WarpLinkTable.h"
#include "../world/alignment/WarpForwardScan.h"
#include "../world/alignment/WarpTraversal.h"
#include "BlockOccupancy.h"
#include "TrainKinematics.h"
#include "TrainLeadingEnd.h"
#include "TrainOccupiedPath.h"
#include "PlatformPassThrough.h"
#include "../world/alignment/Guid.h"
#include "../world/alignment/PlatformRoutine.h"

namespace rs::world {
struct PlatformStopInfo;
struct TrackGraph;
} // namespace rs::world

namespace rs::sim {

// 前方の速度制限の先読み結果(ForwardSpeedLimits)。
struct SpeedLimitLookahead {
    // 今いる区間の制限[km/h]。無制限なら非常に大きい値。最高速度の上限として扱う。
    double inSectionCapKmh = std::numeric_limits<double>::max();
    // 前方で制限が下がる地点(距離は先頭からの残り)。手前の区間より高い/同じ制限の地点は、
    // 手前の区間の上限で満たされるので含めない。自動停止と同じ制動パターンで手前から減速する。
    std::vector<SpeedConstraint> ahead;
};

// レイアウト保存用の走行状態。速度・距離は内部表現どおり m/s・m で保持する。
struct TrainRuntimeSnapshot {
    TrainId trainId = rs::world::NilGuid();
    size_t sceneIndex = 0;
    TrainKinematics kin;
    bool lastTravelReversed = false;
    bool stopTargetReversed = false;
    rs::world::Guid targetPlatform = rs::world::NilGuid();
    rs::world::Guid departedPlatform = rs::world::NilGuid();
};

class TrainRuntime {
public:
    // 接近先の番線に適用される予定のルーチン要素を覗くコールバック(任意)。
    // 要素がstopOffsetNormalizedを持つ場合、その値を停止位置の正規化Offsetとして使う。
    using PlatformRoutinePeek = std::function<const rs::world::PlatformRoutineElement*(
        const rs::world::PlatformStopInfo&)>;
    // 通過が確定した番線のルーチン要素を1つ消費するコールバック(RailSim IIはDequeueが
    // Action判定より前にあるため、通過でも要素は消費される)。
    using PlatformRoutineConsume = std::function<void(const rs::world::PlatformStopInfo&)>;

    // trainIdの編成を目標速度[km/h]で走行させる(0で停止=headを固定。
    // 負値=後退方向への目標速度)。
    // 未配置のgroupIdを指定しても次回Tick時に無視されるだけで安全。
    void SetGroupTargetSpeed(TrainId trainId, double targetSpeedKmh);
    void SetGroupCurrentSpeed(TrainId trainId, double speedKmh);
    double GroupTargetSpeedKmh(TrainId trainId) const;
    double GroupCurrentSpeedKmh(TrainId trainId) const;
    bool GroupLastTravelReversed(TrainId trainId) const;
    // 走行を始めてからの符号付き通算走行距離[m]（前進で増え、後退で減る）。0から始まり、
    // レール・Sceneの切り替えでは戻らない。未走行の編成は0。
    double GroupOdometerM(TrainId trainId) const;
    bool GroupTravelReversed(TrainId trainId) const;
    // 移動前のhead、所属Scene、走行方向をまとめて返す。未初期化の編成はfalse。
    bool GetGroupForwardScanStart(const GraphForScene& graphForScene,
                                  const rs::world::WarpLinkTable& warpTable, TrainId trainId,
                                  LeadingEnd& outLeadingEnd) const;

    // 遠方先読み(ScanForwardSpanAcrossScenes)の直前に呼ぶ、seek集合を使った
    // 分岐開通先の事前解決(rs::world::ResolveSeekAheadPointRoutinesの薄いラッパー)。
    // startScene/startからdsだけ先読みし、その編成がまだ到達していない分岐だけ
    // activeSlotを解決してseek集合へ加える。既に解決済みの分岐は再解決しない。
    void ResolveGroupSeekAheadPointRoutines(
        const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
        const rs::world::WarpLinkTable& warpTable, TrainId trainId,
        const rs::world::Guid& formationId, size_t startScene,
        const rs::world::TrackPosition& start, double ds,
        const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& occupiedBlocks,
        const std::vector<int>& trainTagIds, double nowAbsSeconds,
        const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& previousOccupiedBlocks);

    // trainIdの直近のResolveGroupSeekAheadPointRoutines呼び出しで解決した、列車ごとの
    // 分岐決定(共有activeSlotへは未反映のプレビュー)。エントリが無ければnullptrを返す。
    // 読み取り専用の前方探索(SeekStationStop等)へslotOverrideとして渡すことで、
    // 先読みで一度決めた経路を同じtick内の全ての先読みを使う側が一貫して見られるようにする。
    const rs::world::NodeSlotOverride* GroupSeekDecisions(TrainId trainId) const;

    // 直近tickの加減速度[m/h/s]("_ACCEL"用、RailSim II 仕様の単位。速度の絶対値の
    // tick間差分から算出、丸めなし)。未走行なら0.0。
    double GroupCurrentAccelMhPerS(TrainId trainId) const;

    // 自動停止: trainIdの編成に対し、そこから先remainingMetersだけ
    // 進んだ地点で自動的に速度0になるよう制動を開始させる(targetSpeedのcap自体は
    // 変更しない)。未配置のgroupIdに対しても新規エントリを作る(SetGroupTargetSpeedの
    // 負値/正値時と同じ挙動)。負のremainingMetersは0として扱う。
    void SetGroupStopDistance(TrainId trainId, double remainingMeters);
    // 名前付き停止制約。StationStopはkin.stopDistanceが専用帳簿を持つため無視する。
    // 他の源はラッチしないので、有効な間は呼び出し側が毎tick再供給する。
    void SetGroupStopConstraint(TrainId trainId, rs::sim::StopConstraintSource source,
                                double remainingMeters);
    void ClearGroupStopConstraint(TrainId trainId, rs::sim::StopConstraintSource source);
    // 自動停止目標を解除する(以後は通常のtargetSpeedどおりに走る)。未走行なら無視。
    void ClearGroupStopTarget(TrainId trainId);
    // 今この編成が自動停止目標(SetGroupStopDistance/SeekStationStop)を持っているか。
    // GroupTargetPlatform は一度セットすると別の番線を検出するまで消えないラッチな
    // ので、「今この番線へ減速中」と「そこを発車済み」の区別に使えない。ダイヤ状態
    // 機械(rs::sim::DiaRuntime)の「減速中」判定はこちらを見る。
    bool GroupHasStopTarget(TrainId trainId) const;

    // 停止目標までの残り距離[m](head基準、進行方向に沿ったスカラー)。目標が無い
    // /未走行ならnullopt。接近判定(rs::sim::ComputePlatformApproach)が「停止位置の
    // 先にある踏切/駅は見ない」ためのクランプ値として使う。
    std::optional<double> GroupStopDistanceM(TrainId trainId) const;

    // 直近のTickGroupでSetGroupStopDistanceの目標地点に到達し停止したか。
    // 未走行/目標未設定ならfalse。
    bool GroupArrivedAtStop(TrainId trainId) const;

    // trainIdが走行中エントリを持ち、かつ停止目標(SeekStationStop)を
    // 一度でもセットしたことがあれば、その目標番線のGuidを返す(未設定/未走行
    // ならrs::world::NilGuid())。ドア系システムスイッチ("_DOOR1"/"_DOOR2")で、どの番線のOpenDoor定義を参照すべきかを
    // 呼び出し側(SceneRenderer)が引くために使う。
    rs::world::Guid GroupTargetPlatform(TrainId trainId) const;

    // trainIdが走行中エントリを持つ場合、そのtrainが今属しているSceneの
    // 添字を返す(未走行/未初期化ならstd::nullopt)。呼び出し側(SceneRenderer)が
    // 「アクティブSceneと一致する場合のみregistry/GPUへ書き戻す」ゲートに使う
    // (CPU側のデータ常駐とGPU常駐を分離するため)。
    std::optional<size_t> GroupSceneIndex(TrainId trainId) const;
    void SetGroupOccupiedPathKeepLength(TrainId trainId, double meters);
    const TrainOccupiedPath* GroupOccupiedPath(TrainId trainId) const;
    // 走行中の全編成の占有edge列(空のものは除く)。駅のレール接続エフェクタが使用中ノードを求めるのに使う。
    std::vector<const TrainOccupiedPath*> AllOccupiedPaths() const;

    std::vector<TrainRuntimeSnapshot> Snapshot() const;
    // trainIdsに含まれる編成だけの走行状態(Sceneの削除の控え用)。
    std::vector<TrainRuntimeSnapshot> SnapshotOf(const std::vector<TrainId>& trainIds) const;
    void Restore(const std::vector<TrainRuntimeSnapshot>& snapshots);
    // 今ある走行状態は残したまま、snapshotsの編成を足す(Sceneの削除の取り消し用)。
    void AddSnapshots(const std::vector<TrainRuntimeSnapshot>& snapshots);
    void Clear();
    // Sceneの並べ替え・削除に合わせ、編成の所属Sceneと占有edge列のSceneを付け替える(oldToNew[旧]=新)。
    // 対応先がkRemovedSceneのSceneにいる編成は走行状態ごと捨て、占有edge列はそのSceneに入る所から切る。
    void RemapSceneIndices(const std::vector<size_t>& oldToNew);

    // trainIdの走行状態エントリを消す(編成削除時、SceneRenderer::RemoveGroup用)。
    void RemoveGroup(TrainId trainId);

    // groupIdが走行中(runningGroups_にエントリを持つ)ならkinematicsをdt分進め、
    // outHead/outDsMeters/outSceneIndexを更新してtrueを返す。未走行ならfalseを返し、
    // out引数は変更しない。
    // graphForScene: sceneIndex -> 対応するTrackGraphのポインタ(存在しなければ
    // nullptr)を返すコールバック(rs::app非依存の抽象化、WarpTraversal.hと同じ規約)。
    // 非constポインタを返す:
    // 前進するheadの走査で背向(trailing)通過を検知したら、その面の現在スロットを
    // 進入スロットへ書き戻す(=割り出し)ため、TickGroup自身がグラフを変更する
    // 唯一の書き込み経路になる(後方chase等の読み取り専用呼び出しとは非対称)。
    // warpTable: Scene間のワープ接続テーブル(空でも正しくwarp無しの扱いになる)。
    // currentSceneIndex: このtrainが未初期化(started==false)の場合の初期
    // sceneIndex。呼び出し側はleadPositionが実際に属するSceneの添字を渡すこと
    // (アクティブSceneの添字で代用すると、非アクティブScene上に配置された編成の
    // 初回tickでedge Guidが見つからず永久停止する)。
    // leadPosition: registry上の現在の先頭車位置(head未初期化時の遅延初期化に使う。
    // SetGroupTargetSpeedでの新規エントリはheadが未初期化(デフォルト構築)のままのため、
    // 実際の先頭車位置で初期化してからでないとAdvanceが存在しないedgeを参照して落ちる)。
    bool TickGroup(const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
                  const rs::world::WarpLinkTable& warpTable, size_t currentSceneIndex, double dt,
                  TrainId trainId, const rs::world::TrackPosition& leadPosition,
                  double maxVelocityKmh, double maxAccelerationKmhPerS,
                  double maxDecelerationKmhPerS, rs::world::TrackPosition& outHead,
                  double& outDsMeters, size_t& outSceneIndex,
                  const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& occupiedBlocks = {});
    bool TickGroup(const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
                  const rs::world::WarpLinkTable& warpTable, size_t currentSceneIndex, double dt,
                  TrainId trainId, const rs::world::TrackPosition& leadPosition,
                  double maxVelocityKmh, double maxAccelerationKmhPerS,
                  double maxDecelerationKmhPerS, rs::world::TrackPosition& outHead,
                  double& outDsMeters, size_t& outSceneIndex,
                  const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& occupiedBlocks,
                  const std::vector<int>& trainTagIds, const rs::world::Guid& formationId,
                  double nowAbsSeconds,
                  const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& previousOccupiedBlocks,
                  const rs::world::ForwardScanSpan* forwardSpan = nullptr);

    // 前方maxSearchM以内にstoppableな番線があれば自動的に停止目標をセットする。
    // 停止目標は「編成の中央」がfoundEdgeの
    // 中央に来る位置(head=編成先頭からtrainLengthM/2先のfoundEdge中央)。
    // trainLengthMは編成全長(呼び出し側=SceneRendererが各車両のfrontLimit-
    // tailLimitの総和として渡す、既定0=補正無し)。
    // 既にそのgroupが同じ番線をターゲット中(targetPlatformが一致)なら何もしない
    // (RailSim II のm_Platformガード相当、再セットでarrivedAtStopがフリッカするのを防ぐ)。
    // 未走行のgroupIdは無視。
    void SeekStationStop(
        const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
        const rs::world::WarpLinkTable& warpTable, TrainId trainId, double maxSearchM,
        double trainLengthM = 0.0, const rs::world::ForwardScanSpan* forwardSpan = nullptr,
        const PlatformRoutinePeek& platformRoutinePeek = {},
        const PlatformRoutineConsume& platformRoutineConsume = {},
        const rs::world::NodeSlotOverride* slotOverride = nullptr);

    // 前方maxSearchM以内の速度制限を先読みする。
    // 今いる区間の制限は最高速度の上限として(inSectionCapKmh)、前方で制限が下がる地点は
    // 制約の一覧として(ahead)返す。呼び出し側は、前者を車両側maxVelocityの小さい方としてTickGroupの
    // maxVelocityKmhへ渡し、後者をSetGroupSpeedLimitConstraintsで渡す。
    // SeekStationStopと異なり状態を持たない(targetPlatformのようなラッチは無し)。
    // 速度制限は「区間に入る」「出る」の終端が無く常に現在位置基準で再評価すべき
    // ものなので、毎tick呼び直して結果をそのまま通すだけで自然に解除される
    // (advisor相談で確認: ラッチ化すると解除ロジックとフリッカ対策を新たに持つ
    // 必要が出るため、あえてラッチにしない設計)。
    // headが「区間に入るまでに減速し終える」対象で、編成中央基準の補正は行わない
    // (駅停車のtrainLengthM補正とは異なる仕様。先頭到達時点で
    // 制限速度以下になっていればよいものとして扱う)。
    // 未走行なら無制限・制約なし。
    // signalSpeedはこの列車に適用される信号の制限速度(MakeSignalSpeedForTrainFn)。空なら信号を無視する。
    SpeedLimitLookahead ForwardSpeedLimits(
        const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
        const rs::world::WarpLinkTable& warpTable, TrainId trainId, double maxSearchM,
        const rs::world::ForwardScanSpan* forwardSpan = nullptr,
        const rs::world::NodeSlotOverride* slotOverride = nullptr,
        const rs::world::SignalSpeedForTrainFn& signalSpeed = rs::world::SignalSpeedForTrainFn()) const;
    // ForwardSpeedLimitsのaheadを、次のTickGroupが使う制約として覚える。毎回丸ごと置き換える
    // (空を渡すと解除)。未走行のtrainIdは無視。
    void SetGroupSpeedLimitConstraints(TrainId trainId,
                                       std::vector<rs::sim::SpeedConstraint> constraints);

    // 移動閉塞モードの制約。停止余裕はこの層で適用する。
    std::optional<double> ComputeMovingBlockConstraint(
        const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
        const rs::world::WarpLinkTable& warpTable, TrainId trainId,
        const std::vector<OccupancyTrain>& occupancyTrains, double maxSearchM,
        const rs::world::ForwardScanSpan* forwardSpan = nullptr,
        const rs::world::NodeSlotOverride* slotOverride = nullptr, double dtSeconds = 0.0) const;

private:
    // 走行中のtrainId -> キネマティクス状態(head/speed)+今属しているScene。
    // 速度0にしてもエントリ自体は残す(GroupCurrentSpeedKmh等の参照用)。
    struct RunningTrain {
        size_t sceneIndex = 0;
        TrainKinematics kin;
        bool lastTravelReversed = false;
        // 走行を始めてからの符号付き通算走行距離[m]。TickGroupが進めた距離をそのまま足す。
        double odometerM = 0.0;
        // StationStop以外の停止制約。StationStop添字は常にnulloptのまま使う。
        std::array<std::optional<double>, static_cast<size_t>(rs::sim::StopConstraintSource::Count)>
            stopConstraints{};
        // 前方で速度を下げる地点(SetGroupSpeedLimitConstraints)。距離は直近に渡された時点の残り。
        std::vector<rs::sim::SpeedConstraint> speedLimitConstraints;
        rs::world::Guid targetPlatform = rs::world::NilGuid();
        // targetPlatform/kin.stopDistanceを算出したときの走行方向。停止目標は
        // 「その向きへ進んだときの残り距離」なので、向きが変わると意味を失う
        // (TrainKinematicsはstopDistanceをstd::abs(ds)で減らすため、逆走へ
        // 転じても残距離が減り続け、駅間の無関係な地点で停止してしまう)。
        bool stopTargetReversed = false;
        // 直前に発車した番線。発車直後はheadがまだその番線のedge上にいるため、
        // SeekStationStopが同じ番線を距離0で再捕捉して即停車扱いになる
        // (FindForwardEdgeは開始edge自身をdistance=0でfoundにする約束事)。
        // headがこの番線のedgeから外れるまで再捕捉を禁止し、外れた時点で解除する。
        rs::world::Guid departedPlatform = rs::world::NilGuid();
        rs::world::Guid passingPlatform = rs::world::NilGuid();
        bool passingEntered = false;
        double prevSpeedKmh = 0.0;
        double accelMhPerS = 0.0;
        TrainOccupiedPath occupiedPath;
        double occupiedPathKeepLengthM = 0.0;
    };

    // この編成が今どちら向きに走る意図かを返す(true=後退)。速度が0でも意図を
    // 失わないよう、speed→targetSpeed→直近の走行方向の順に決める。停車中の編成の
    // 前方探索(駅・信号・速度制限・閉塞)が常に順方向を向いてしまうのを防ぐための処理。
    static bool TravelReversed(const RunningTrain& running);

    // 占有edge列をhead起点の後方走査で作り直す。
    static void SeedOccupiedPath(RunningTrain& running, const GraphForScene& graphForScene,
                                 const rs::world::WarpLinkTable& warpTable);
    // TickGroupの前進結果を占有edge列へ反映する(前進は先頭側を伸ばし、後退は先頭側を消費して後端を延長)。
    static void UpdateOccupiedPathAfterAdvance(RunningTrain& running,
                                               const rs::world::WarpAdvanceResult& result,
                                               bool reversing, const GraphForScene& graphForScene,
                                               const rs::world::WarpLinkTable& warpTable);
    // 背向通過したポイント面の現在スロットを進入スロットへ書き戻す(割り出し)。
    static void WriteBackTrailingPasses(
        const std::function<rs::world::TrackGraph*(size_t)>& graphForScene,
        const rs::world::WarpAdvanceResult& result);

    // SeekStationStopの前半: 発車済み番線のedgeから編成がまるごと出たら
    // running.departedPlatformをNilへ戻し、再捕捉を解禁する。
    void ReleaseDepartedPlatformIfLeft(const GraphForScene& graphForScene,
                                       const rs::world::WarpLinkTable& warpTable,
                                       RunningTrain& running,
                                       double trainLengthM);

    // SeekStationStopの後半: 見つかった番線への停止距離(チェーン
    // 解決不能時はfoundEdge中央近似)を返す。
    static double ComputeStationStopDistance(
        const rs::world::TrackGraph& targetGraph,
        const rs::world::PlatformStopInfo& targetPlatform,
        const rs::world::TrackPosition& scanStart,
        const rs::world::WarpForwardScanResult& result, double maxSearchM,
        double trainLengthM, std::optional<double> stopOffsetNormalized);

    std::map<TrainId, RunningTrain> runningGroups_;
    // 分岐ルーチンの層別・編成別実行時カーソル。Undo対象外。
    rs::world::PointRoutineCursors pointRoutineCursor_;
};

} // namespace rs::sim
