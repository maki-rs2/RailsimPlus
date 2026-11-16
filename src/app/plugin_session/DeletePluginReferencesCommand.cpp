#include "DeletePluginReferencesCommand.h"

#include <utility>

namespace rs::app {

DeletePluginReferencesCommand::DeletePluginReferencesCommand(PluginReferenceContext context,
                                                             PluginReferenceSite site, std::string folderUtf8,
                                                             std::vector<std::string> pluginRoots)
    : pluginRoots_(std::move(pluginRoots)), context_(context), site_(site), folderUtf8_(std::move(folderUtf8)) {
    context_.pluginRoots = &pluginRoots_;
}

void DeletePluginReferencesCommand::Apply(rs::world::TrackGraph& graph) {
    (void)graph;  // 対象はcontext_が指す実体。Editor::graphと同じものを指す
    deletedCount_ = DeletePluginReferences(context_, site_, folderUtf8_, log_);
}

void DeletePluginReferencesCommand::Revert(rs::world::TrackGraph& graph) {
    (void)graph;
    RestoreDeletedPluginReferences(context_, log_);
}

}  // namespace rs::app
