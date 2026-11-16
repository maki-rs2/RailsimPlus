#include "BlockSignalCleanupCommand.h"

#include <algorithm>
#include <utility>

#include "../../world/alignment/BlockRegistry.h"
#include "../../world/alignment/SpeedSignalRegistry.h"

namespace rs::editor {

ClearBlockSignalEditsCommand::ClearBlockSignalEditsCommand(std::vector<rs::world::Guid> edges)
    : edges_(std::move(edges)) {}

void ClearBlockSignalEditsCommand::Apply(rs::world::TrackGraph& graph) {
    savedReferences_.clear();
    for (const rs::world::Guid& edge : edges_) {
        rs::world::TrackGraphReferenceChange change;
        change.firstEdge = edge;
        savedReferences_.push_back(rs::world::RemapTrackGraphReferences(graph, change));
    }
}

void ClearBlockSignalEditsCommand::Revert(rs::world::TrackGraph& graph) {
    for (auto it = savedReferences_.rbegin(); it != savedReferences_.rend(); ++it)
        rs::world::RestoreTrackGraphReferences(graph, *it);
}

}  // namespace rs::editor
