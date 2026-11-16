#include "PointOpeningAction.h"

#include "ToastQueue.h"
#include "../../editor/tool/Editor.h"
#include "../../world/alignment/PointRoutineResolve.h"

namespace rs::app {

bool ChangePointOpening(ToastQueue& queue, rs::editor::Editor& editor, const rs::world::Guid& nodeId, int linkIndex,
                        const std::unordered_set<rs::world::Guid, rs::world::GuidHash>& occupiedBlocks) {
    const auto found = editor.graph.nodes.find(nodeId);
    if (found == editor.graph.nodes.end() || !found->second.IsPoint()) return false;
    const rs::world::TrackNode& node = found->second;
    if (linkIndex < 0 || linkIndex >= static_cast<int>(node.links.size())) return false;
    if (node.IsLinkActive(linkIndex)) return true;
    if (rs::world::IsNodeAdjacentToOccupiedBlock(editor.graph, node, occupiedBlocks)) {
        queue.Post(ToastSeverity::Warning, "列車がいるので切り替えられません");
        return false;
    }
    return editor.TrySetPointActiveLink(nodeId, linkIndex);
}

} // namespace rs::app
