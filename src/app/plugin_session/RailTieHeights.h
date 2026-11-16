//
// RailTieHeights.h - edgeのtrackPluginsからRail/Tie積み上げ高さを求める。
//
// 確定edge(TrackEdge::trackPlugins)・敷設プレビュー(Editor::layingSettings.defaultTrackPlugins)・
// .rs2取り込み時の標高正規化(Rs2SceneApplier.cpp)の3箇所が同じロジックを参照する
// (レール頭頂面の持ち上げ量計算の共通化)。
//
#pragma once

#include <string>
#include <vector>

#include "PluginSelector.h"
#include "../../plugins/schema/PluginTypeTraits.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {

struct RailTieHeights {
    float maxRailHeight = 0.0f;
    float maxTieHeight = 0.0f;

    //レール頭頂面の、地面(ノード標高)からの持ち上げ量
    float TopLift() const { return maxRailHeight + maxTieHeight; }
};

// 1つのプラグイン定義(Rail/Tie)のHeightを、最大値として h へ積む。Rail/Tie以外・レール情報なしは何もしない。
void AccumulateRailTieHeight(RailTieHeights& h, rs::legacy_pi::PluginType type,
                             const rs::legacy_pi::PluginDefinition& def);

// trackPlugins(folderUtf8のリスト)に含まれるRail/Tieエントリの最大Heightを求める。
RailTieHeights ComputeRailTieHeights(const std::vector<std::string>& trackPlugins,
                                     const PluginSelector& selector);

// 押し出しメッシュ(Rail/Tie/Girder)を、レール頭頂面(TopLift)を基準にしたローカル座標で
// 作るときの、上下方向の移動量。Tieは下にRail分、Girderはさらに下にTie分沈む(RailSim II準拠)。
// 種別の表(PluginTypeTraits)と高さだけで決まる。
float ExtrusionOffsetY(rs::legacy_pi::PluginType type, const RailTieHeights& h);

// entries直接版(selectorを持たない呼び出し元向け。TrackPluginPlacement.cpp/
// PlacementMaterialize.cpp/ViewportRailMeshBuild.cppのようにentriesのみを
// 受け取る関数がPole配置のrailTopLift相当を都度算出する用途)。
RailTieHeights ComputeRailTieHeights(const std::vector<std::string>& trackPlugins,
                                     const std::vector<TrackPluginEntry>& entries);

//graphの全edgeのrailTopLiftを、そのedgeが有効なRail/Tieの高さから求めて書き込む。
//列車の車軸位置はこの値を参照するため、列車の最初の姿勢計算より前に呼ぶ
//(メッシュ再構築だけに任せると、読込直後の1フレームは0のまま車体が低く描かれる)。
//呼ぶ前に、graphが使うレール系プラグインをentriesへ読み込んでおくこと。
void UpdateEdgeRailTopLifts(rs::world::TrackGraph& graph,
                            const std::vector<TrackPluginEntry>& entries);

} // namespace rs::app
