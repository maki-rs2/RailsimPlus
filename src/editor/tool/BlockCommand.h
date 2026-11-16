#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Command.h"
#include "../../world/alignment/BlockRegistry.h"
#include "../../world/alignment/SpeedSignalRegistry.h"

namespace rs::editor {

// 閉塞(Block)の追加。guidは呼び出し側(SignalBlockAction)が発行済みの状態で渡す
// (Undo→Redoでの安定性のため、Apply側では自動発行しない)。
class AddBlockCommand : public ICommand {
public:
    explicit AddBlockCommand(rs::world::Block block);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
private:
    rs::world::Block block_;
    size_t index_ = 0;
};

class RemoveBlockCommand : public ICommand {
public:
    explicit RemoveBlockCommand(rs::world::Guid blockId);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
private:
    rs::world::Guid blockId_;
    rs::world::Block block_;
    size_t index_ = 0;
    bool removed_ = false;
};

// edgesとname。値を持つ項目だけ変える。
struct BlockPropertyDelta {
    std::optional<std::vector<rs::world::Guid>> edges;
    std::optional<std::string> name;
};

// edgesを変えると、edgeを外した結果、担当閉塞がedgeを含まなくなった信号も消す(Revertで戻る)。
class SetBlockPropertyCommand : public ICommand {
public:
    SetBlockPropertyCommand(rs::world::Guid blockId, BlockPropertyDelta delta);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
private:
    rs::world::Guid blockId_;
    BlockPropertyDelta delta_;
    BlockPropertyDelta oldValues_;
    bool hadOld_ = false;
    // edgesを変えたApplyの前の信号。担当を失った信号を消した分をRevertで戻す。
    std::optional<std::vector<rs::world::SpeedSignal>> savedSpeedSignals_;
};

} // namespace rs::editor
