//
// WarpLinkTable.h - シーン間ワープ接続テーブル
//
// warpDummy edgeは生成しない(node間テレポート方式)ため、warp接続は
// TrackGraph(Scene毎に別インスタンス)ではなくこの独立テーブルに持つ。
// GUI非依存・ヘッドレス数値検証可能な純データ型(TrackTraversal.h と同じ層)。
//
// 実行時キーは (Scene配列の添字, nodeGuid)。永続化ではシーンをGUIDで参照し、
// 読込時に配列の添字へ戻す。走査中にGUIDからシーンを引き直さずに済む。
//
#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

#include "TrackGraph.h"

namespace rs::world {

// warpリンクの片側端点。sceneIndex は LayoutSession 側のScene配列インデックス。
struct WarpEndpoint {
    size_t sceneIndex = 0;
    Guid   node       = NilGuid();
};

// 実行時ルックアップ用キー: (sceneIndex, nodeGuid)。
struct WarpNodeKey {
    size_t sceneIndex = 0;
    Guid   node       = NilGuid();
};

inline bool operator==(const WarpNodeKey& a, const WarpNodeKey& b) {
    return a.sceneIndex == b.sceneIndex && a.node == b.node;
}

struct WarpNodeKeyHash {
    size_t operator()(const WarpNodeKey& key) const noexcept {
        size_t h = GuidHash{}(key.node);
        // sceneIndex を混ぜる(FNV風、GuidHash と同じ乗数を流用)。
        h ^= std::hash<size_t>{}(key.sceneIndex) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return h;
    }
};

// 1本のwarpリンク(両端点、順不同)。
struct WarpLink {
    WarpEndpoint a;
    WarpEndpoint b;
};

// warp接続の実行時テーブル。1nodeは高々1本のwarpリンクの端点にしかならない
// 想定(RailSim II も同様)。同じ端点へ2本目を AddLink した場合は後勝ちで
// 古いリンクの反対側の逆引きが残らないよう明示的に上書きする。
class WarpLinkTable {
public:
    // (sceneA, nodeA) <-> (sceneB, nodeB) を双方向に登録する。
    // 既に片方の端点が別リンクに属していた場合は後勝ちで上書きする
    // (相手側の逆引きエントリも合わせて更新するため片側だけ残る不整合は起きない)。
    void AddLink(const WarpEndpoint& endpointA, const WarpEndpoint& endpointB);

    // 指定した2端点のリンクを取り除く(AddWarpLinkCommand::revert用)。該当リンクが
    // 無ければ何もしない。
    void RemoveLink(const WarpEndpoint& endpointA, const WarpEndpoint& endpointB);

    // (sceneIndex, node) の相手側端点を引く。未登録なら std::nullopt。
    std::optional<WarpEndpoint> Find(size_t sceneIndex, const Guid& node) const;

    // 全リンクの列挙(オーサリングUI・永続化用)。
    const std::vector<WarpLink>& Links() const { return links_; }

    void Clear();

private:
    std::vector<WarpLink> links_;
    std::unordered_map<WarpNodeKey, WarpEndpoint, WarpNodeKeyHash> lookup_;
};

} // namespace rs::world
