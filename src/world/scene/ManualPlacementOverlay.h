//
// ManualPlacementOverlay.h - 配置物(架線柱/橋脚/架線)の手動編集を表す差分オーバーレイ
//
// 架線柱(Pole)・橋脚(Pier)・架線(Line)の自動配置は stateless に再導出される
// (CollectTrackPluginPlacements / WalkPoleRuns)。この使い捨て設計を保ったまま、
// ユーザーの手動編集(個別削除・手動追加・架線の手動接続)だけを「自動配置へ
// 後段で重ねる永続差分」として表現するのがこのオーバーレイ。全ての編集は
// edge 上の弧長 s でアンカーされ、線形編集に追従する(ワールド座標では凍結しない)。
//
// .rsl へそのまま書き出せるよう、GUIDで安定させ、値型・浅いネストで持つ。
//
#pragma once

#include <string>
#include <vector>

#include "../alignment/TrackGraph.h"
#include "ScenePlacement.h"
#include "RunAnchor.h"

namespace rs::world {

// 実体化済みedge×folderの個体(Pole/Pier)。完全リスト。
struct PlacedIndividual {
    Guid        id = NilGuid();
    Guid        edge = NilGuid();
    double      s = 0.0;
    int         direction = 1;
    double      lateralOffsetM = 0.0;
    // 軌道面フレームからの高さ。実体化時点の描画値を固定する。
    double      verticalOffsetM = 0.0;
    double      yawOffsetDeg = 0.0;
    std::string folderUtf8;
    // edge/s に依存せず、ワールド座標へ直接アンカーする個体かどうか。
    bool           hasWorldAnchor = false;
    rs::core::Vec3 worldPos;
    float          worldYawDeg = 0.0f;
};

// 架線(Pole個体ペア間のwire)。完全リスト。両端はPlacedIndividual::idを参照する。
struct WireSpan {
    Guid        id = NilGuid();
    Guid        poleA = NilGuid();
    Guid        poleB = NilGuid();
    // Pole側の架線接続点添字(WireLateralOffsetsForの並び順)。
    int         branchA = 0;
    int         branchB = 0;
    std::string lineFolderUtf8;
};

// scene が所有する手動オーバーレイ全体。空(全ベクタ空)なら自動配置と完全に
// 一致する(自動配置と同じ結果になることを確認するため、手動配置が無いうちは常に空で挿入する)。
struct ManualPlacementOverlay {
    std::vector<PlacedIndividual> poles;
    std::vector<PlacedIndividual> piers;
    std::vector<WireSpan>         lines;
    std::vector<RunAnchor> runAnchors;

    bool Empty() const {
        return poles.empty() && piers.empty() && lines.empty() && runAnchors.empty();
    }
};

} // namespace rs::world
