//
// EdgeCommandLinks.h - edge追加/削除コマンドが共有するnodeのlinks操作(editor/tool内部用)
//
#pragma once

#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

// node の links から edgeGuid の Link を除去する。activeSlot は「その面での出現順」なので、
// link を1本抜くと同じ面の以降のスロット番号がずれる。両面について本数に収まる範囲へ丸める。
void RemoveLinkFromNode(rs::world::TrackNode& node, const rs::world::Guid& edgeGuid);

}  // namespace rs::editor
