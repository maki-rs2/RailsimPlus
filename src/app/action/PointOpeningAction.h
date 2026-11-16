//
// PointOpeningAction.h - 分岐の開通先を手で切り替える操作(左タブの開通先の行と、3D画面の左右の印が共通で呼ぶ)
//
// 分岐につながるレールのどれかに列車がいるときは転換しない。在線の判定は走行中の分岐と同じ
// IsNodeAdjacentToOccupiedBlock を使う。
//
#pragma once

#include <unordered_set>

#include "../../world/alignment/Guid.h"

namespace rs::editor {
class Editor;
} // namespace rs::editor

namespace rs::app {
class ToastQueue;

// occupiedBlocksは今列車がいる閉塞(走行していなければ空)。linkIndexは分岐の links の添字。
// すでにその開通先なら何もせずtrue。在線中や分岐でないときはfalse(在線中だけトーストで知らせる)。
bool ChangePointOpening(ToastQueue& queue, rs::editor::Editor& editor, const rs::world::Guid& nodeId, int linkIndex,
                        const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& occupiedBlocks);

} // namespace rs::app
