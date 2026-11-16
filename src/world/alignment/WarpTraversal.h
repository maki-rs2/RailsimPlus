//
// WarpTraversal.h - Scene跨ぎ(warp)の合成走査エントリポイント
//
// 既存の Advance(TrackTraversal.h) はシグネチャ・実装とも変更しない純関数の
// ままとし、その上位でwarpテーブルを引いてScene境界をまたぐ処理をここに
// 分離する(TrackTraversal.cpp を神クラス化させないための別ファイル分割)。
// GUI非依存・ヘッドレス数値検証可能な純関数群(rs::app には依存しない)。
//
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "TrackTraversal.h"
#include "WarpLinkTable.h"

namespace rs::world {

// Scene跨ぎ走査中に発生したtrailing pass。TrailingPass自体はどのSceneのグラフに
// 属するか判別できないため、発生元のsceneIndexで包む。呼び出し側はsceneIndexで対応するgraphを引いてから適用する。
struct WarpTrailingPass {
    size_t       sceneIndex = 0;
    TrailingPass pass;
};

struct WarpVisitedEdge {
    size_t sceneIndex = 0;
    VisitedEdge edge;
    // 前のedgeからワープで飛んで入ったedge(同じSceneの中のワープも含む)。
    bool viaWarp = false;
};

// Scene跨ぎ走査の結果。sceneIndex は最終位置が属するScene。
struct WarpAdvanceResult {
    size_t        sceneIndex = 0;
    TrackPosition pos;
    bool   atEnd = false;
    double consumed = 0.0;
    int    warpHops = 0;  // 何回warpをまたいだか(テスト・デバッグ用)
    std::optional<double> firstWarpDistance;
    // 停止地点がwarpリンクの端点nodeでありながら、相手Scene側へ渡れずに走査を
    // 打ち切った(相手graph/node/edgeが引けなかった)。この場合 sceneIndex は
    // 手前側のSceneのままなので、「相手Sceneへ渡らなかった」と「まだ手前Sceneに
    // 収まっている」を呼び出し側が区別できるようにフラグで返す。
    bool   truncatedAtWarp = false;
    // 各Scene内でのtrailing pass記録をwarp跨ぎ順に連結したもの。
    std::vector<WarpTrailingPass> trailingPasses;
    std::vector<WarpVisitedEdge> visitedEdges;
};

// startScene/start から ds(>=0)だけ前進する。既存 Advance を各Scene graphで
// 呼び、atEnd で止まった地点の端点nodeが warpTable に登録されていれば相手
// Scene側へジャンプして残り距離で走査を継続する。
//
// graphForScene: sceneIndex -> 対応する TrackGraph のポインタ(存在しなければ
// nullptr)を返すコールバック。LayoutSession 等 rs::app 側の型へ直接依存
// しないための抽象化。
WarpAdvanceResult AdvanceAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable,
    size_t startScene, TrackPosition start, double ds);

// startScene/start から distance(>=0)だけ「後方」(進行方向と逆)へ辿る。
// 戻り値 pos.dir は start.dir と同じ「車両前進方向」規約を保つ
// (rs::sim::ChaseBackward(TrainVehicle.h)のScene跨ぎ版)。
// trailingPasses は常に空にして返す: 背向(trailing)判定は前進するheadの走査
// でのみ意味を持ち、グラフへ適用する唯一の経路は TrainRuntime::TickGroup
// (前進側)である。後方chaseの結果を適用すると誤った割り出しになる。
WarpAdvanceResult ChaseBackwardAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const WarpLinkTable& warpTable,
    size_t startScene, TrackPosition start, double distance);

} // namespace rs::world
