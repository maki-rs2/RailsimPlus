#pragma once

#include <optional>
#include <vector>

#include "Command.h"
#include "../../world/alignment/SpeedSignalRegistry.h"

namespace rs::editor {

// 速度制限の信号(SpeedSignal)の追加。guidは呼び出し側(SignalBlockAction)が発行済みの状態で
// 渡す(Undo→Redoでの安定性のため、Apply側では自動発行しない)。置けるかの検証
// (ValidateSpeedSignalPlacement)は呼び出し側がExecute前に行う。
class AddSpeedSignalCommand : public ICommand {
public:
    explicit AddSpeedSignalCommand(rs::world::SpeedSignal signal);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
private:
    rs::world::SpeedSignal signal_;
};

class RemoveSpeedSignalCommand : public ICommand {
public:
    explicit RemoveSpeedSignalCommand(rs::world::Guid signalId);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
private:
    rs::world::Guid signalId_;
    rs::world::SpeedSignal signal_;
    size_t index_ = 0;
    bool removed_ = false;
};

// 位置(edge, s)・向き・担当閉塞(block)・段(stages)の変更。
struct SpeedSignalPropertyDelta {
    std::optional<rs::world::Guid> edge;
    std::optional<rs::world::Guid> block;
    std::optional<double> s;
    std::optional<int> direction;
    std::optional<std::vector<rs::world::SignalAspectStage>> stages;
    std::optional<std::vector<int32_t>> switchOptions;
};

class SetSpeedSignalPropertyCommand : public ICommand {
public:
    SetSpeedSignalPropertyCommand(rs::world::Guid signalId, SpeedSignalPropertyDelta delta);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
private:
    rs::world::Guid signalId_;
    SpeedSignalPropertyDelta delta_;
    SpeedSignalPropertyDelta oldValues_;
    bool hadOld_ = false;
};

} // namespace rs::editor
