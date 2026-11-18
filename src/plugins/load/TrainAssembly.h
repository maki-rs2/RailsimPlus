//
// TrainAssembly.h - PluginDefinition と rs::sim::TrainVehicle を橋渡しするアダプタ層。
//
// PrimaryAssembly の Axle/Body パーツを rs::sim::TrainVehicle（純粋な走行シミュレーション
// データ、PI プラグインの概念に非依存）へ変換し、軌道上へ解決した結果を全パーツ
// （Object3D 等の付属物を含む）のワールド行列へ展開する。Turn（メッシュ/接続点の Z 軸
// 鏡像）・Joint3D 付属物の追従は rs::sim が感知しないため、すべてこの層が担う。
//
#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "../../core/Math.h"
#include "../../sim/TrainVehicle.h"
#include "../../sim/TrainOccupiedPath.h"
#include "../../world/alignment/TrackPosition.h"
#include "../../world/alignment/WarpTraversal.h"

namespace rs::legacy_pi { struct PluginDefinition; }

namespace rs::legacy_plugin {

// def.parts と同じ並びで、各パーツの追従方法を持つテンプレート（プラグインごとに
// 1回構築すればよい静的情報。軌道位置には依存しない）。
struct TrainVehicleTemplate {
    rs::sim::TrainVehicle vehicle; // 2点固定を解決できる Axle/Body のみを格納

    // vehicle.axles[i] / vehicle.bodies[i] に対応する def.parts の添字。
    std::vector<int> axlePartIndex;
    std::vector<int> bodyPartIndex;
    // vehicle.bodies[i] の Turn（PI の Turn=yes）。rs::sim は感知しないため
    // 描画側で別途180°回転を合成する（Turn はメッシュ/接続点のZ軸鏡像）。
    std::vector<bool> bodyTurn;

    // def.parts と同じ並びの、全パーツの追従情報。
    struct PartRenderInfo {
        enum class AnchorKind { None, Axle, Body } anchorKind = AnchorKind::None;
        int  anchorIndex = -1;     // vehicle.axles / vehicle.bodies の添字
        bool isAnchorSelf = false; // true: このパーツ自身が axle/body（Turn合成のみ）
        // isAnchorSelf==false のとき使用: このパーツの静的配置(worldMat)を
        // アンカーの静的配置基準へ変換する相対行列(row-major, v'=v*M)。
        rs::core::Mat4 deltaMat = rs::core::Mat4::Identity();
    };
    std::vector<PartRenderInfo> partRender;
};

// PluginDefinition（BuildPluginDefinition 呼び出し後、PrimaryAssembly 解決済み）から
// テンプレートを構築する。Axle/Body 以外や JointZY が2本でない Body は
// 追従対象外（Joint3D 参照で他パーツに委譲、無ければ末尾の Body＝通常は車体に委譲）。
// 前提: Axle→台車→車体の順（下位が先）に宣言されている標準構成。
TrainVehicleTemplate BuildTrainVehicleTemplate(const rs::legacy_pi::PluginDefinition& def);

// axle.spinFraction(車輪回転位相[0,1)turn)の永続状態。tmpl.vehicle.axles と
// 同じ要素数、呼び出し側(main.cpp 等)が車両1両につき1つ確保・使い回す
// （rs::sim::SpinAxles 参照）。
using AxleSpinState = std::vector<double>;

// 解決済みの車軸1本の接地点。車輪音の継目判定が使う。
struct AxleContact {
    rs::world::TrackPosition on;  // 車軸が乗っている軌道位置
    rs::core::Vec3 worldPos;      // 車軸のワールド位置（レール踏面）
    double chaseOffsetM = 0.0;    // 車両headからの後方chase距離（rs::sim::AxleChaseOffset）
    bool wheelSound = true;       // 車輪音を鳴らす車軸か
};

// head（先頭車 FrontLimit の軌道位置）を基準に全パーツのワールド行列を解決する。
// 戻り値は def.parts と同じ並び・同じ要素数（row-major, v'=v*M 規約）。
// dsMeters/axleSpinState を渡すと車輪回転(RailSim IIのCAxleObject::CalcRotation
// 移植)を反映する。axleSpinStateはnullptrまたは要素数不一致なら常に無効(回転0)。
// tiltState を渡すと車体の振り子Tilt(RailSim IIのCTrain::ApplyAxle/
// CBodyObject::SetPosture移植)を反映する。tiltStateは車両1両につき1つ、
// 呼び出し側(main.cpp 等)が編成の各両ごとに確保・使い回す永続状態（再配置時は
// TiltDriverState::valid=false に戻すこと）。nullptrならTiltは常に無効。
// tiltKc/tiltKr は目視合わせ係数（RailSim II 内部の一部係数は未取得のため既定1.0で、
// UIから調整できる, rs::sim::SolveBodies 参照）。maxTiltAngleOut を渡すと、この
// 呼び出しで計算した全body中の傾斜角絶対値の最大[rad]を書き込む（実際の描画を
// 見る前に数値で信号の生死・大きさを確認できるようにするための計測用出力。
// hasTiltなbodyが無ければ0のまま）。
// axleContactsOut を渡すと、vehicle.axles と同じ並びで車軸の接地点を書き込む。
// reversed=true: 編成内でこの車両が前後反転していることを表す。
// rs::sim::TrainVehicle::flipped へそのまま橋渡しし、軸/台車のchase解決自体
// (SolveAxles/SolveBodies/SpinAxles)が反転を正しく反映する(TrainVehicle.h/.cpp
// 参照)。各軸は反転有無に関わらず常に軌道曲線上のchase位置(headからのオフセット
// のみ符号が変わる)に解決されるため、直線・曲線のどちらでも歪まない。
// (解決後に車両中心1点を軸とした180°剛体回転を後段で一括適用する方式は、
// 点対称反射のため直線軌道でしか元の曲線に乗らず、曲線上で反転車両が外側へ
// ずれるので使わない)。
std::vector<rs::core::Mat4> SolvePartWorlds(const rs::world::TrackGraph& graph,
                                             const rs::world::TrackPosition& head,
                                             const TrainVehicleTemplate& tmpl, double dt = 0.0,
                                             double dsMeters = 0.0,
                                             AxleSpinState* axleSpinState = nullptr,
                                             rs::sim::TiltDriverState* tiltState = nullptr,
                                             double tiltKc = 1.0, double tiltKr = 1.0,
                                             double* maxTiltAngleOut = nullptr,
                                             bool reversed = false,
                                             const rs::sim::TrainOccupiedPath* occupiedPath = nullptr,
                                             double pathBaseOffsetM = 0.0,
                                             std::vector<AxleContact>* axleContactsOut = nullptr);

// パーツのアンカーを選び、配置空間から物理姿勢への変換を返す。アンカーが無ければ false。
bool FindPartPlacementToWorld(const rs::legacy_pi::PluginDefinition& def,
    const TrainVehicleTemplate& tmpl, const std::vector<rs::core::Mat4>& solved,
    size_t partIndex, rs::core::Mat4& out);

// SolvePartWorlds の結果(solved)へ、可動部込みで解き直した行列(dynamicWorlds、
// rs::app::ResolveDynamicPartWorlds の戻り値。プラグインのローカル座標)を合成する。
// 各パーツを「dynamicWorlds × アンカーの静的配置の逆 × アンカーの物理姿勢」で置くので、
// 親の可動部の動きが Joint3D の子へ伝わり、ZY系の可動部も今フレームの姿勢で解ける。
// 3つの配列はどれも def.parts と同じ並び。
std::vector<rs::core::Mat4> ComposeDynamicPartWorlds(
    const rs::legacy_pi::PluginDefinition& def, const TrainVehicleTemplate& tmpl,
    const std::vector<rs::core::Mat4>& solved,
    const std::vector<rs::core::Mat4>& dynamicWorlds);

// 編成内の1両ぶんの間隔計算用スペック(異種車両対応)。frontLimit/tailLimit
// はその車両自身の連結位置[m](TrainVehicleTemplate::vehicle由来)。reversedは向き
// 反転フラグ(間隔計算には影響しない。連結面ギャップ無し前提=既存の簡易化を踏襲)。
struct CarSpec {
    double frontLimit = 0.0, tailLimit = 0.0;
    bool reversed = false;
};

// 編成（車両ごとに異なるテンプレート・反転可）の各車両 head を解決する。
// heads[0]=head（引数）。heads[i+1] は heads[i] から cars[i] 自身の車両長
// (frontLimit-tailLimit)だけ後方(chase)へ辿った位置＝前車 TailLimit と
// 次車 FrontLimit が突き合う位置。
std::vector<rs::world::TrackPosition> SolveTrainHeads(const rs::world::TrackGraph& graph,
                                                          const rs::world::TrackPosition& head,
                                                          const std::vector<CarSpec>& cars);

// 編成内1両ぶんの「今どのSceneにいるか」込みの解決結果。
struct CarPose {
    size_t sceneIndex = 0;                 // この車両が属するScene(=車両headのいるScene)
    rs::world::TrackPosition head;         // そのSceneのgraph上での車両head(FrontLimit位置)
    // 前端(head)と後端(head から車両長だけ後方)の間にwarpの不連続がある状態。
    // 別Sceneへ渡った場合だけでなく、同一Scene内で閉じたwarpリンクを跨いだ場合
    // (warpHops>0)も含む。この間に片側のgraphだけで車軸を解くと
    // 全車軸がwarp端点nodeへ張り付き、body の forward=normalize(0)=(0,0,0) に
    // なって姿勢がおかしくなるため、呼び出し側(描画)はこの車両の描画自体を
    // スキップする。
    bool spansScenes = false;
    // warp跨ぎフェードのα。warp端点へ近づく側の端が kWarpFadeLengthM 以内に
    // 入ると1→0へ減衰する。跨いでいる間は0。フェードイン(抜けた後)側は経過距離の
    // 累積が要るため呼び出し側(SceneTrainFade)が持つ。
    float fadeAlpha = 1.0f;
    // この車両headが編成先頭headから何m後方かの弧長。占有edge列(TrainOccupiedPath)は
    // 編成先頭head起点で距離を測るため、車軸解決の基準としてこの値を足す。
    double pathOffsetM = 0.0;
};

// warp跨ぎフェードの長さ[m]。
// 最短の車両長より十分短く、warpの片側で同時にフェード中の車両が高々1両になる値。
constexpr double kWarpFadeLengthM = 5.0;

// SolveTrainHeads のScene跨ぎ版。連結順(先頭→後方)に各車両の head と
// 所属Sceneを解決する。cars[i] の車両長ぶん後方へ ChaseBackwardAcrossScenes で
// 辿り、その結果が次の車両の head になる(既存 SolveTrainHeads と同じ連結規約)。
// 既存 SolveTrainHeads は最後の車両のchaseを省くが、こちらは最終車両の
// spansScenes 判定に後方chaseの結果が要るため全車両ぶん行う。
std::vector<CarPose> SolveTrainHeadsAcrossScenes(
    const std::function<const rs::world::TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, size_t headSceneIndex,
    const rs::world::TrackPosition& head, const std::vector<CarSpec>& cars,
    bool travelReversed = false, const rs::sim::TrainOccupiedPath* occupiedPath = nullptr);

} // namespace rs::legacy_plugin
