//
// RailLinkEdit.h - edge端同士の接続の付け替え(ConnectRail / BranchRail / DisconnectRail の本体)
//
// 駅プラグインのレール接続エフェクタが行う操作を、軌道グラフ上の純関数として持つ。
// 変更は graph.railLinkOverlay にだけ書き、graph.nodes・TrackEdge::linkNodes(静的な姿、保存・Undo対象)
// には触れない。edge の形(spline)も変えない。つながり具合は FindLinkNode / LinkNodeOfEnd で引く。
//
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "TrackGraph.h"

namespace rs::world {

// edgeの1端(end: 0=nodeA側 / 1=nodeB側)。
struct EdgeEnd {
    Guid edge = NilGuid();
    int end = 0;

    bool operator==(const EdgeEnd&) const = default;
};

// ノードを列車が使用中か。使用中のノードをつなぎ替える操作は何もしない。
using NodeInUseFn = std::function<bool(const Guid& node)>;

// 2つの端をつなぐ。両端の既存の接続を外し、a の端の位置に作った新しいノードの面0へ a、面1へ b をつなぐ。
// すでに a のノードの反対面に b の edge がつながっている・どちらかが使用中・端が解決できない
// ときは何もしない。変えたら true。
bool ConnectEdgeEnds(TrackGraph& graph, const EdgeEnd& a, const EdgeEnd& b, const NodeInUseFn& inUse);

// a のノードの反対面へ b の端を分岐として足す。b の既存の接続は外す。反対面がすでに2本・
// すでにつながっている・どちらかが使用中のときは何もしない。変えたら true。
bool BranchEdgeEnds(TrackGraph& graph, const EdgeEnd& a, const EdgeEnd& b, const NodeInUseFn& inUse);

// a の端を、反対面の接続から切り離して単独の新しいノードへつなぎ直す。反対面が空・使用中のときは
// 何もしない。変えたら true。
bool DisconnectEdgeEnd(TrackGraph& graph, const EdgeEnd& a, const NodeInUseFn& inUse);

// 駅配置(sourceAddress)の indexInStation 番目の番線の端点を edge の端へ引く。
// end 0 は番線の先頭レールの始点側、end 1 は末尾レールの終点側(RailSim II の
// GetRailWayFront / GetRailWayBack)。番線が無い・レールが無いときは nullopt。
std::optional<EdgeEnd> ResolvePlatformEnd(const TrackGraph& graph, const std::string& stationAddress,
                                          int indexInStation, int end);

} // namespace rs::world
