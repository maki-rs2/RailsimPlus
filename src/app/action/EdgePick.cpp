#include "EdgePick.h"
#include "../../editor/tool/Editor.h"

#include <algorithm>

namespace rs::app {

std::vector<rs::world::Guid> ApplyEdgePickToList(std::vector<rs::world::Guid> current,
                                                 const EdgePickEvent& event) {
    const bool toggle = event.kind == EdgePickEvent::Kind::Click ||
                        event.kind == EdgePickEvent::Kind::ShiftTap;
    for (const rs::world::Guid& edge : event.edges) {
        const std::vector<rs::world::Guid>::iterator it = std::find(current.begin(), current.end(), edge);
        if (it == current.end()) {
            current.push_back(edge);
        } else if (toggle) {
            current.erase(it);
        }
    }
    return current;
}

void ApplyEdgePickToSelection(rs::editor::Editor& editor, const EdgePickEvent& event) {
    switch (event.kind) {
    case EdgePickEvent::Kind::Click:
        if (!event.edges.empty()) editor.SelectEdge(event.edges.front());
        break;
    case EdgePickEvent::Kind::ShiftTap:
        for (const rs::world::Guid& edge : event.edges) editor.ToggleEdgeSelection(edge);
        break;
    case EdgePickEvent::Kind::Box:
        editor.SelectEdges(event.edges);
        break;
    case EdgePickEvent::Kind::None:
        break;
    }
}

} // namespace rs::app
