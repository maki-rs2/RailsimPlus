//
// DynamicJointPlacement.h - Joint3D階層を可動部(mover)込みで毎フレーム動的解決する。
//
// SchemaPlacement.cpp::resolveJoint3D はパース時に1回だけ、Joint3Dで参照される
// 側(アタッチ先)の StaticMove/StaticRotation を考慮せず静的にワールド行列を
// 確定する。そのため、アタッチ先パーツ自身がスイッチ連動の可動部を持つ場合
// (例: 一部の駅プラグインに見られる、スイッチで実体が切り替わるダミーパーツが
// 「ホーム幅」スイッチで StaticMove する)、そこへ Joint3D で接続する
// 子パーツ(屋根/柵/階段等)が
// 追従しなくなる。
//
// このモジュールは resolveJoint3D と同じ反復パス構造・幾何計算
// (rs::legacy_pi::ComposeJoint3DWorld)を使い、Joint3D参照先の「今フレームの
// 実行時ワールド行列(自身のmover込み)」を使って毎フレーム再解決する。
// 非表示のアンカーダミー(ModelFileNameを持たずJoint3Dの基準名としてのみ
// 存在するパーツ)も解決対象に含める必要があるため、表示中パーツのみの
// LoadedPlugin::assemblyPlacements ではなく、plugin.def.parts(全パーツ)を
// 走査する。
//
#pragma once

#include <vector>

#include "../../../core/Math.h"
#include "../../schema/PluginSchema.h"
#include "../../schema/SwitchExpr.h"
#include "MoverRuntime.h"

namespace rs::legacy_plugin {

std::vector<rs::core::Mat4> UpdatePartMoverLocals(
    const rs::legacy_pi::PluginDefinition& def,
    std::vector<PartMoverRuntime>& moverRuntimes,
    const rs::legacy_pi::SwitchResolver& resolve, uint64_t switchGeneration, float dt,
    float dynamicRotateSpeedScale, double clockSecondsOfDay);

} // namespace rs::legacy_plugin

namespace rs::app {

// plugin.def.parts と同じ並びで、各パーツの「今フレームのワールド行列
// (自身のmover適用 × Joint3D階層合成)」を返す。moverRuntimes は
// plugin.def.parts と同じ並びの配列(PluginSession::moverRuntimes)を想定し、
// GroupCount()>0 のパーツについて UpdateActivation/Update/ComposeLocal を
// 呼ぶ副作用を持つ(アニメ位相を進める。1フレームにつき1回だけ呼ぶこと)。
std::vector<rs::core::Mat4> ResolveDynamicPartWorlds(
    const rs::legacy_pi::PluginDefinition& def,
    std::vector<rs::legacy_plugin::PartMoverRuntime>& moverRuntimes,
    const rs::legacy_pi::SwitchResolver& resolve, uint64_t switchGeneration, float dt,
    float dynamicRotateSpeedScale, double clockSecondsOfDay);

// 静的インスタンス化の焼き込み用。moverRuntimesをBuildし直し(前回の位相を引き継がない)、
// 発動判定(UpdateActivation)の後、Pre/ReverseDelayと位相をすべて目標まで進め切ってから
// ResolveDynamicPartWorldsと同じ並びのワールド行列を返す。時間駆動の可動部
// (DynamicRotate/AnalogClock)は静止しないので、回転速度0・時刻0の初期姿勢のまま使い、
// 静止判定の対象から外す(焼いた絵が時間で変わることはない。静的インスタンス化の対象は
// 時間駆動の可動部を持たないプラグインだけ)。
// 上限回数でも静止しなければwarnを出してそのときの状態で返す。
// switchGeneration: Build直後のキャッシュ(~0)と異なる値を渡すこと。
std::vector<rs::core::Mat4> ResolveSettledPartWorlds(
    const rs::legacy_pi::PluginDefinition& def,
    std::vector<rs::legacy_plugin::PartMoverRuntime>& moverRuntimes,
    const rs::legacy_pi::SwitchResolver& resolve, uint64_t switchGeneration);

} // namespace rs::app
