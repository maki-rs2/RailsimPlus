//
// Rs2Import.h - Rs2Layout(生データ) → rs::world::TrackGraph(内部表現) 変換
//
// Address(hex文字列)は2パスで解決する: パス1で全 RailConnector/RailWay の
// Address→Guid を採番し、パス2で Link/PointInst 等の相互参照を解決する。
// 解決できない参照はエラーにせず warnings へ記録し、該当リンクを未接続扱いにする。
//
// 生成する TrackEdge は spline(既存消費側 RailMeshBuilder/TrackTraversal/描画/
// ピッキングが参照)に加え、curve も Rs2CurveFit::FitCurveFromSplitters で
// 復元する(RailSim II は緩和曲線を持たず直線+純円弧のみのため復元可能)。ただし refit.available は既定false(3点フィレット由来のedge専用)のまま。
//
#pragma once

#include <string>
#include <vector>

#include "Rs2NodeBuild.h"
#include "Rs2DiaImport.h"
#include "Rs2Types.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/scene/ScenePlacement.h"
#include "../../world/time/GameClock.h"
#include "../../world/time/SolarPosition.h"

namespace rs::legacy_layout {

struct Rs2PendingTrainRoutines {
    std::vector<PendingPointTrainRoutine> points;
    std::vector<PendingPlatformTrainRoutine> platforms;
};

// Struct/Pole/Pier/Station の1配置。app(rs::world::SceneEntity)には依存しない
// DTO(plugins/layoutはappへ依存しないレイヤリング規約)。
// pluginFolderUtf8 は解決済み絶対パス(空なら不足プラグイン、Report対象)。
struct ImportedPlacement {
    std::string             kind;  // "Struct"/"Pole"/"Pier"/"Station"/"Surface"
    std::string             pluginFolderUtf8;
    rs::world::FreePlacement placement;
    // レイアウト保存時点のスイッチ確定値(Rs2PlacedInstance::switchOptionsそのまま)。
    std::vector<int32_t>    switchOptions;
    // 保存時メモリアドレス(Rs2PlacedInstance::addressそのまま)。TrackEdge::importedPiers/importedPolesにedge厳密紐付け済みのPier/Poleを
    // 判別し、静的Scenery配置としての二重焼き込みを避けるために使う。
    std::string              address;
};

// Rs2Scene::placements(Struct/Pole/Pier/Station/Surface)を ImportedPlacement へ変換する。
// installRootUtf8 配下の <kind>/<pluginName> へフォルダを解決する(kindがそのまま
// 種別フォルダ名: Struct/Station/Pole/Pier/Surface)。Dir/Up は DecomposeFreePlacement で
// yaw/pitch/rollへ変換する(Poleはhasup=falseのためワールドup(0,1,0)を既定補完)。
// 解決失敗(pluginNameが空)のインスタンスは pluginFolderUtf8="" のまま返し、
// warnings に記録する(プレースホルダ表示するかは呼び出し側の判断)。
std::vector<ImportedPlacement> ImportPlacements(const Rs2Scene& scene,
                                                const std::string& installRootUtf8,
                                                std::vector<std::string>& warnings);

// 1 Scene の軌道網(RailConnector/RailWay)を TrackGraph へ変換する。
// installRootUtf8: .rs2 のインストールルート(Rail/Tie/Girder/Pier/Line/Pole/Layout の
// 親フォルダ)。RailWay の RailPlugin 等(プラグイン名のみ)を
// installRootUtf8/<種別フォルダ>/<プラグイン名> の絶対パスへ解決して
// TrackEdge::trackPlugins へ積む(値はフォルダ名そのもの)。空文字列のプラグイン参照(未指定)はスキップする。
// warnings には解決失敗・想定外パターン(fanout>4等)を記録する(エラーにはしない)。
// outAddresses が非nullなら、この呼び出しで構築したAddress→Guid解決マップを書き戻す
// (WarpList取り込み(Rs2WarpImport)がAddress→Guidをレイアウト横断で引くために使う)。
// outPendingTrainRoutines が非nullなら、TrainGroup別ポイント・停車ルーチンの未解決参照を書き戻す。
rs::world::TrackGraph ImportTrackGraph(const Rs2Scene& scene, const std::string& installRootUtf8,
                                       std::vector<std::string>& warnings,
                                       AddressMap* outAddresses = nullptr,
                                       Rs2PendingTrainRoutines* outPendingTrainRoutines = nullptr);

// RailWay のプラグイン参照名(フォルダ名のみ)を installRootUtf8 配下の絶対パスへ解決する。
// kindDir は "Rail"/"Tie"/"Girder"/"Pier"/"Line"/"Pole" のいずれか。pluginName が空なら
// 空文字列を返す(=未指定、trackPluginsへ追加しない)。
std::string ResolveTrackPluginFolder(const std::string& installRootUtf8, const char* kindDir,
                                    const std::string& pluginName);

// edge の形を両端ノードの姿勢から作れなかったときに warnings へ積む文言の先頭部分
// (取り込み後に件数を数えるために固定)。その edge は Splitter 由来の形のまま残る。
inline constexpr const char* kShapeBuildFallbackWarning =
    "edge形状の生成に失敗したためRailSim IIのSplitter形状で代替";

// Splitter列(疎なサンプル点)を3次エルミートで再サンプリングし、
// TrackNodeSample列(密なサンプル点)を生成する。直線区間(隣接Splitterの接線が
// 実質同一)は端点2個のみ、曲線区間は既定1.0m刻みで細分する。
// 戻り値の TrackNodeSample::cant は 0 のまま(呼び出し側が両端ノードのCantから
// 弧長線形補間して埋める)。samples.size()<2 の場合は空を返す。
std::vector<rs::world::TrackNodeSample> ResampleSplitterList(
    const std::vector<Rs2Splitter>& splitters);

// RailSim II のTimeScale/SimulationSpeedラジオボタンindex(0-9/0-4)を実際の
// 「ゲーム秒/実秒」倍率へ変換するテーブル(CSimulationMode.cpp SCALE_TABLE/
// SPEED_TABLE)。
inline constexpr int kTimeScaleTable[10] = {2880, 1400, 480, 288, 144, 72, 48, 24, 6, 1};
inline constexpr int kSimSpeedTable[5] = {0, 1, 2, 10, 100};

// Rs2Time(生データ)から GameClock の日時/TimeScale と SolarOverrides
// (自転/公転の強制モード)を設定する。time.present==falseなら何もしない。
// SumDaysはDateとの整合性検証のみに使い(不一致はwarningsへ記録)、
// GameClockの日時はDate(year/month/day)を正として決定する
// (CSaveFile.cppのSumDaysはDateと連動する年初起算の通算日数であり、
// 権威はDate側にあるため)。
void ApplyRs2TimeToGameClock(const Rs2Time& time, rs::world::GameClock& clock,
                              rs::world::SolarOverrides& overrides,
                              std::vector<std::string>& warnings);

} // namespace rs::legacy_layout
