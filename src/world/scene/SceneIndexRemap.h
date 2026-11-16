//
// SceneIndexRemap.h - Scene番号の付け替え表(旧番号→新番号)の共通の扱い
//
// Scene番号を持つ状態(ワープ・編成の所属・予約など)は、並べ替え・削除・削除の取り消しのたびに
// 同じ形の表で付け替える。消えるSceneは kRemovedScene で表し、持ち主ごとに「そのSceneを指すものを捨てる」。
//
#pragma once

#include <cstddef>
#include <limits>
#include <vector>

namespace rs::world {

// 付け替え表で「対応先なし(そのSceneは消える)」を表す値。
inline constexpr size_t kRemovedScene = std::numeric_limits<size_t>::max();

// 範囲外の番号(壊れたデータ)は動かさず、そのまま返す。消えるSceneは kRemovedScene を返す。
inline size_t RemapSceneIndex(const std::vector<size_t>& oldToNew, size_t index) {
    return index < oldToNew.size() ? oldToNew[index] : index;
}

inline bool IsRemovedScene(size_t index) { return index == kRemovedScene; }

}  // namespace rs::world
