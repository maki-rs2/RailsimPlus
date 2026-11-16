//
// PlacementRescaleCommand.h - edge の長さが変わる編集に、配置物の位置の追従を足すコマンド
//
// 手動編集された配置物(架線柱・橋脚・アンカー)は edge 上の距離 s を持つ。edge の形が
// 作り直されて全長が変わると、区間(pluginSections など)は再構築の中で長さの比で
// 追従するが、overlay は TrackGraph の外にあるので追従しない。このコマンドは、中身の
// コマンドを実行する前後で全 edge の長さを比べ、変わった edge の配置物を同じ比で
// 動かす。Revert では overlay を実行前の状態へ戻す(比の逆算はしない)。
//
// 対象は edge の形だけが変わる編集(ノード移動・接線・標高・カント・緩和曲線)。edge の
// 分割・結合・追加のように edge の構成そのものが変わる編集には使わない。
//
#pragma once

#include <memory>

#include "../../world/scene/ManualPlacementOverlay.h"
#include "Command.h"

namespace rs::editor {

class Editor;

class RescalePlacementCommand : public ICommand {
public:
    RescalePlacementCommand(rs::world::ManualPlacementOverlay& overlay,
                            std::unique_ptr<ICommand> inner);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
    bool Applied() const override { return inner_->Applied(); }
    bool Changed() const override { return inner_->Changed(); }

private:
    rs::world::ManualPlacementOverlay& overlay_;
    std::unique_ptr<ICommand> inner_;
    rs::world::ManualPlacementOverlay savedOverlay_;
};

// cmd を stack で実行する。overlay が非 null なら RescalePlacementCommand で包む。
// 積んだら true(失敗・値が変わらないときは false)。
bool ExecuteWithPlacementRescale(Editor& editor,
                                 rs::world::ManualPlacementOverlay* overlay,
                                 std::unique_ptr<ICommand> cmd);

} // namespace rs::editor
