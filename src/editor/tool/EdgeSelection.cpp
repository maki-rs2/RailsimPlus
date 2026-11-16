//
// EdgeSelection.cpp - 選択中のレール(edge)の集合
//
#include "EdgeSelection.h"

namespace rs::editor {

using rs::world::Guid;

bool EdgeSelection::RetainExisting(const rs::world::TrackGraph& graph) {
    const std::size_t before = Count();
    std::vector<Guid> retained;
    for (const Guid& edge : Edges()) {
        if (graph.edges.find(edge) != graph.edges.end()) retained.push_back(edge);
    }
    SetItems(retained);
    return Count() != before;
}

} // namespace rs::editor
