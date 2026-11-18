#pragma once

#include <vector>

#include "Command.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::editor {

// 指定edge群を参照するSpeedSignalの削除と、Block::edgesからの除去を行う。除去後にedgesが
// 空になったBlockも削除し、blocks/speedSignals全体をUndo用に保存する。
class ClearBlockSignalEditsCommand : public ICommand {
public:
    explicit ClearBlockSignalEditsCommand(std::vector<rs::world::Guid> edges);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;

private:
    std::vector<rs::world::Guid> edges_;
    std::vector<rs::world::TrackGraphReferenceSnapshot> savedReferences_;
};

}  // namespace rs::editor
