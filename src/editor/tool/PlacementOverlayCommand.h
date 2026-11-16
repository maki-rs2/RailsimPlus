//
// PlacementOverlayCommand.h - 配置オーバーレイ(ManualPlacementOverlay)を変更する
// Undo可能コマンド。
//
// overlayはTrackGraphの外にある状態なので、CommandStackはこれを自動では復元しない。
// edge/node削除にともなう個別編集の除去はCommandStackの外で行われており、Undoしても
// 戻らなかった。除去をこのコマンド経由にして、削除コマンドと同じcompositeへ積む。
//
#pragma once

#include <vector>

#include "../../world/scene/ManualPlacementOverlay.h"
#include "Command.h"

namespace rs::editor {

// 指定edge群をアンカーとする実体化済み配置をoverlayから除去する。Revertで
// 除去前の状態を戻す。
class ClearPlacementEditsCommand : public ICommand {
public:
    ClearPlacementEditsCommand(rs::world::ManualPlacementOverlay& overlay,
                               std::vector<rs::world::Guid> edges);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;

private:
    rs::world::ManualPlacementOverlay& overlay_;
    std::vector<rs::world::Guid> edges_;

    rs::world::ManualPlacementOverlay savedOverlay_;
};

} // namespace rs::editor
