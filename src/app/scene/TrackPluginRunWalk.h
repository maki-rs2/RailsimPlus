//
// TrackPluginRunWalk.h - レール系edge独立エントリのrun走査プリミティブとPole径間走査。
//
// チェーン→run分割・絶対弧長→edge/localS変換といった共通
// プリミティブ(CollectTrackPluginPlacementsのPier配置とWalkPoleRunsのPole配置の
// 両方が使う)と、たわみ量制限付き可変間隔アルゴリズムによるPole配置点
// 走査(WalkPoleRuns)をここに置く。
//
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "../../core/Math.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../world/alignment/TrackChain.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../../world/scene/RunAnchor.h"
#include "../plugin_session/PluginSelector.h"

namespace rs::app {

// edgeのpluginSectionsにfolderUtf8が含まれるか(区間は
// 問わない、run/chainの区間分割=どのedgeがこのentryに参加するかの粗い判定にのみ
// 使う。edge内のs範囲での実際の絞り込みはEntryActiveAtが担う)。
bool EdgeHasEntry(const rs::world::TrackEdge& edge, const std::string& folderUtf8);

// edge内プラグイン区間。graph上のedge+弧長sの1点が、folderUtf8の
// 有効範囲(飛び地があれば複数)内にあるか。edgeが無い/範囲が無いなら false。
// Pier固定間隔配置・Pole径間走査の両方で、生成した各点をedge内のs範囲で最終
// フィルタするために使う(run/chain境界の粗い判定=EdgeHasEntryとは別)。
bool EntryActiveAt(const rs::world::TrackGraph& graph, const rs::world::Guid& edge, double s,
                   const std::string& folderUtf8);

// 複線一括(trackNum>1)エントリの配置先チェーン: 中心線(束ねる本数は問わない)
// edge を各1-edge chain にして返す。既存の run/interval 配置ロジックへ
// そのまま流せる(factoring)。単線用は呼び出し側で実レールchainを使う。
std::vector<rs::world::EdgeChain> CenterlineChainsFor(const rs::world::TrackGraph& graph,
                                                      int trackNum);

// チェーン内の1edge分の弧長情報(累積開始位置つき)。
struct ChainSegment {
    rs::world::Guid edge = rs::world::NilGuid();
    int direction = 1;
    float length = 0.0f;
    float cumStart = 0.0f;
};

std::vector<ChainSegment> BuildChainSegments(const rs::world::EdgeChain& chain,
                                             const rs::world::TrackGraph& graph, float& outTotalLength);

// segs[i..j) の範囲内でチェーン先頭からの弧長absSに対応するseg・edge内localSを
// 求める(FrameAtAbsS/TrackPositionAtAbsS共通のseg探索コア)。
struct AbsSLocation {
    const ChainSegment* seg = nullptr;
    float localS = 0.0f;
};

// segs[start..) からpred(edge)を満たす区間(run)を探す。[i,j)がrun本体、
// i==segs.size()ならこれ以上runが無い。Pier/Poleの両方のrun分割ループが使う
// 共通コア(述語だけが異なる)。
template <typename Pred>
std::pair<size_t, size_t> FindNextRun(const std::vector<ChainSegment>& segs, size_t start, Pred pred) {
    size_t i = start;
    while (i < segs.size() && !pred(segs[i].edge)) ++i;
    size_t j = i;
    while (j < segs.size() && pred(segs[j].edge)) ++j;
    return {i, j};
}

AbsSLocation LocateAbsS(const std::vector<ChainSegment>& segs, size_t i, size_t j, float absS);

// segs[i..j) の範囲内でチェーン先頭からの弧長absSに対応するedge/localSを求め、
// EvaluateAt()で軌道フレーム(位置・接線・アップ)を返す。Pier/Poleの両方の
// 配置ループから使う共通ヘルパー。
rs::world::TrackFrame FrameAtAbsS(const std::vector<ChainSegment>& segs, size_t i, size_t j,
                                  const rs::world::TrackGraph& graph, float absS);

// segs[i..j) の範囲内でabsSに対応するTrackPositionを求める(SceneEntity配置用。
// FrameAtAbsSと同じseg探索をTrackPosition向けに行う)。
rs::world::TrackPosition TrackPositionAtAbsS(const std::vector<ChainSegment>& segs, size_t i, size_t j,
                                             float absS);

// Pole配置1本ぶん(SceneEntity配置用のTrackPositionと、その地点の生の
// 軌道フレーム)。verticalOffsetMは含まない(呼び出し側が用途に応じて加算する:
// GPUインスタンス化=TrolleyAlt+Height、架線メッシュ=TrolleyAltのみ)。
// absSはrun先頭(runStart)からの絶対弧長[m]。runが複数edgeにまたがる場合、
// position.s(edge起点基準)だけでは点同士の前後順を跨edge比較できないため、
// 絶対値で持つ。
struct PoleWalkPoint {
    rs::world::TrackPosition position;
    rs::core::Vec3 pos, dir, up;
    float absS = 0.0f;
    float manualLateralOffsetM = 0.0f;
};

// runが辿った実edge列(絶対弧長の基準)。手動追加Pole(edge,localS)をrunの
// absSへ変換するために公開する(合流処理用)。
struct PoleRunEdgeSpan {
    rs::world::Guid edge = rs::world::NilGuid();
    int   direction = 1;
    float cumStart = 0.0f; // run先頭からの絶対弧長でのこのedge開始位置
    float length = 0.0f;
};

// 1つのPole×Lineエントリの組について、run(そのentryペアが有効なedgeの連続区間)
// 単位で走査済みのPole配置点列。
struct PoleWalkRun {
    const TrackPluginEntry* poleEntry = nullptr;
    const TrackPluginEntry* lineEntry = nullptr;
    std::string runKey; // "run開始edgeのhex:runインデックス"(安定id生成用)
    std::vector<PoleWalkPoint> points; // run内、走査順
    std::vector<PoleRunEdgeSpan> edgeSpans; // run内の実edge列(絶対弧長の基準)
};

// entries内の全Pole×Line entryの組について、たわみ量制限付き可変間隔
// アルゴリズム(RailSim IIのCLineBuildCurve::FinishTrace()準拠)でPoleの配置点を走査する。
// Line未選択(hasLineInfo=false)の組は結果に含まれない。戻り値は
// CollectTrackPluginPlacements(GPUインスタンス化)と、EditorViewport.cppの
// 架線メッシュ生成(隣接points間をワイヤーで結ぶ)の両方から使う共有結果。
// entriesのポインタを保持するため、戻り値の使用中はentriesを生存させること。
std::vector<PoleWalkRun> WalkPoleRuns(const std::vector<TrackPluginEntry>& entries,
                                      const rs::world::TrackGraph& graph,
                                      const std::vector<rs::world::RunAnchor>& runAnchors = {});

} // namespace rs::app
