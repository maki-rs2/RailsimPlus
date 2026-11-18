#include "ReplacePluginReferencesCommand.h"

#include <utility>
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {

ReplacePluginReferencesCommand::ReplacePluginReferencesCommand(PluginReferenceContext context,
                                                               PluginReplacePlan plan,
                                                               std::vector<std::string> pluginRoots)
    : pluginRoots_(std::move(pluginRoots)), context_(context), plan_(std::move(plan)) {
    context_.pluginRoots = &pluginRoots_;
}

void ReplacePluginReferencesCommand::Apply(rs::world::TrackGraph& graph) {
    (void)graph;  // 対象はcontext_が指す実体。Editor::graphと同じものを指す
    replacedCount_ = ReplacePluginReferences(context_, plan_, &log_);
}

void ReplacePluginReferencesCommand::Revert(rs::world::TrackGraph& graph) {
    (void)graph;
    RestorePluginReferences(context_, log_);
}

}  // namespace rs::app
