// RailLinkOverlay.h - 実行中のレール接続状態を静的 TrackGraph に重ねる表。
#pragma once

#include <cstdint>
#include <cstddef>
#include <unordered_map>

#include "Guid.h"

namespace rs::world {

struct TrackGraph;
struct TrackEdge;
struct TrackNode;

struct RailLinkEnd {
    Guid edge = NilGuid();
    int end = 0;
    bool operator==(const RailLinkEnd&) const = default;
};

struct RailLinkEndHash {
    std::size_t operator()(const RailLinkEnd& key) const noexcept {
        return GuidHash{}(key.edge) ^ (static_cast<std::size_t>(key.end) * 0x9e3779b9u);
    }
};

struct RailLinkOverlay {
    std::unordered_map<RailLinkEnd, Guid, RailLinkEndHash> linkNodeOverrides;
    std::unordered_map<Guid, TrackNode, GuidHash> effectiveNodes;
    std::unordered_map<Guid, TrackNode, GuidHash> originalNodes;
    uint64_t generation = 0;

    bool Empty() const;
    // 中身を全て消す。世代は戻さず進める(キャッシュの無効化に使うため)。空なら何もしない。
    void Clear();
    void SetLinkNodeOverride(const RailLinkEnd& key, const Guid& node);
    void SetEffectiveNode(const Guid& node, const TrackNode& effective, const TrackNode& original);
};

const TrackNode* FindLinkNode(const TrackGraph& graph, const Guid& node);
TrackNode* FindLinkNodeMutable(TrackGraph& graph, const Guid& node);
Guid LinkNodeOfEnd(const TrackGraph& graph, const TrackEdge& edge, int end);

// 静的ノードを編集するための写しを返す(無ければ静的ノードから作る)。静的ノードは変えない。
// 静的にも表にも無ければ nullptr。
TrackNode* EnsureEffectiveNode(TrackGraph& graph, const Guid& node);

// 静的グラフに無い新しいノードを表にだけ置く。
void AddOverlayNode(TrackGraph& graph, const TrackNode& node);

// 静的グラフが編集されて、表が古くなっていないか。次のどれかなら古い:
// 上書きの対象edgeが無い / 上書き先のノードがどこにも無い / 静的ノードの写しの元が今の静的ノードと
// 違う(links が変わった・ノードが消えた) / 写しの links が存在しないedgeを指す。
bool IsRailLinkOverlayStale(const TrackGraph& graph);

// 表が古ければ全て消す(継ぎ目は互いにつながるので部分的には残さない)。消したら true。
// 静的グラフの編集の後と、Scene切替・読込のときに呼ぶ。
bool PruneRailLinkOverlay(TrackGraph& graph);

} // namespace rs::world
