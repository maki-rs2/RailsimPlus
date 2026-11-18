#include "Command.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

using rs::world::TrackGraph;

// ============================= AddWarpLinkCommand =============================
AddWarpLinkCommand::AddWarpLinkCommand(rs::world::WarpLinkTable& table, rs::world::WarpEndpoint a,
                                        rs::world::WarpEndpoint b)
    : table_(table), a_(a), b_(b) {}

void AddWarpLinkCommand::Apply(TrackGraph& /*graph*/) { table_.AddLink(a_, b_); }

void AddWarpLinkCommand::Revert(TrackGraph& /*graph*/) { table_.RemoveLink(a_, b_); }

// ============================ RemoveWarpLinkCommand ===========================
RemoveWarpLinkCommand::RemoveWarpLinkCommand(rs::world::WarpLinkTable& table,
                                              rs::world::WarpEndpoint a,
                                              rs::world::WarpEndpoint b)
    : table_(table), a_(a), b_(b) {}

void RemoveWarpLinkCommand::Apply(TrackGraph& /*graph*/) { table_.RemoveLink(a_, b_); }

void RemoveWarpLinkCommand::Revert(TrackGraph& /*graph*/) { table_.AddLink(a_, b_); }

} // namespace rs::editor
