//
// EdgeSelection.h - 選択中のレール(edge)の集合
//
// 選んだ順を保つ。最後に選んだ1本(Primary)が「主」で、区間編集や緩和曲線のように
// 1本を前提にする操作と、ボタンを出す位置の基準に使う。
// UI状態でありUndo対象ではない。graphの存在確認はしない(Editorが必要なときに
// RetainOnlyで刈り込む)。
//
#pragma once

#include "OrderedGuidSelection.h"

namespace rs::editor {

class EdgeSelection : public OrderedGuidSelection {
public:
    const std::vector<rs::world::Guid>& Edges() const { return Items(); }
    // graph.edgesに無いものを外す。外したものがあればtrue。
    bool RetainExisting(const rs::world::TrackGraph& graph);
};

} // namespace rs::editor
