//
// FormationCarSlots.h - 編成の絵の `+`(車両を挿入する位置)の計算
//
// スロットkは「k番目の車両の前」で、carCountは末尾の後ろ。車両n両の編成にはスロットが0〜nのn+1個ある。
// 強調の判定とドロップの実行が同じ関数を使い、判定を1か所に集める。RmlUiに依存しない。
//
#pragma once

#include <cstddef>
#include <optional>

namespace rs::app::workspace {

// 車両fromをスロットslotへ動かしたときの移動後の添字。動かしても並びが変わらない
// (スロットがfromの前か後ろ)ときや、範囲外のときは値なし。
inline std::optional<std::size_t> ResolveCarMoveTarget(std::size_t carCount, std::size_t from, std::size_t slot) {
    if (from >= carCount || slot > carCount) return std::nullopt;
    if (slot == from || slot == from + 1) return std::nullopt;
    // fromより後ろのスロットは、fromを抜いた分だけ前へずれる。
    return slot > from ? slot - 1 : slot;
}

// 車両を新しく挿入できるスロットか(範囲内か)。
inline bool IsValidInsertSlot(std::size_t carCount, std::size_t slot) {
    return slot <= carCount;
}

} // namespace rs::app::workspace
