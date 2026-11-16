#include "TrackPluginSetAction.h"

#include <algorithm>
#include <utility>

#include "../../editor/tool/Editor.h"
#include "../plugin_session/PluginSelector.h"
#include "../plugin_session/RailwayPluginSetApplier.h"
#include "../scene/TrackPluginPlacement.h"

namespace rs::app {
namespace {

// PI定義に間隔が無い(0以下)離散配置レールの表示用の既定間隔[m]。
constexpr float kDefaultPierIntervalMeters = 20.0f;

bool ContainsPath(const std::vector<std::string>& folders, const std::string& folderUtf8) {
    return std::any_of(folders.begin(), folders.end(),
                       [&folderUtf8](const std::string& folder) { return SamePluginPath(folder, folderUtf8); });
}

} // namespace

ErrorSeverity SeverityOf(TrackPluginSetStatus status) {
    switch (status) {
    // 入力や対象を直せば再試行できる。
    case TrackPluginSetStatus::LoadFailed:
    case TrackPluginSetStatus::RejectedMaterialized:
    case TrackPluginSetStatus::TemplateLoadFailed:
    case TrackPluginSetStatus::Ok:
        return ErrorSeverity::Recoverable;
    }
    return ErrorSeverity::Critical;
}

bool TrackPluginSetAction::TargetsSection() const {
    return editor_.selectedEdge != rs::world::NilGuid() &&
           editor_.graph.edges.find(editor_.selectedEdge) != editor_.graph.edges.end();
}

std::size_t TrackPluginSetAction::SectionIndex() const {
    return editor_.SelectedSectionIndex(rs::editor::SectionLayer::PluginSet);
}

std::vector<std::string> TrackPluginSetAction::CurrentFolders() const {
    if (!TargetsSection()) return editor_.layingSettings.defaultTrackPlugins;
    if (editor_.SelectedEdgeCount() > 1) {
        // 複数選択: 選択中の全レールの全区間に入っているものだけを「今のセット」とする。
        // ここから足す・外すと全レールに作用し、全レールで同じ結果になる。
        std::vector<std::string> common;
        bool first = true;
        for (const rs::world::Guid& guid : editor_.SelectedEdges()) {
            const auto it = editor_.graph.edges.find(guid);
            if (it == editor_.graph.edges.end() || it->second.centerlineOnly) continue;
            for (const auto& span : it->second.pluginSections.Spans()) {
                if (first) {
                    common = span.value.plugins;
                    first = false;
                    continue;
                }
                std::erase_if(common, [&span](const std::string& folder) {
                    return !ContainsPath(span.value.plugins, folder);
                });
            }
        }
        return common;
    }
    const std::vector<rs::world::IntervalList<rs::world::PluginSet>::Span>& spans =
        editor_.graph.edges.at(editor_.selectedEdge).pluginSections.Spans();
    const std::size_t index = SectionIndex();
    if (index < spans.size()) return spans[index].value.plugins;
    return {};
}

bool TrackPluginSetAction::ReplaceFolders(std::vector<std::string> folders) {
    if (TargetsSection() && editor_.SelectedEdgeCount() > 1) {
        // 今のセット(全レール共通分)との差を、全レールの全区間へ適用する。
        const std::vector<std::string> current = CurrentFolders();
        std::vector<std::string> add;
        std::vector<std::string> remove;
        for (const std::string& folder : folders)
            if (!ContainsPath(current, folder)) add.push_back(folder);
        for (const std::string& folder : current)
            if (!ContainsPath(folders, folder)) remove.push_back(folder);
        if (add.empty() && remove.empty()) return true;
        return editor_.EditFoldersOnSelectedEdges(add, remove);
    }
    if (TargetsSection()) return editor_.SetSelectedEdgeSectionFolders(SectionIndex(), std::move(folders));
    editor_.SetLayingSetting(&rs::editor::TrackLayingSettings::defaultTrackPlugins, std::move(folders));
    return true;
}

TrackPluginSetViewModel TrackPluginSetAction::ViewModel() const {
    TrackPluginSetViewModel viewModel;
    if (TargetsSection()) {
        viewModel.target = TrackPluginSetTarget::Section;
        viewModel.selectedEdgeCount = editor_.SelectedEdgeCount();
        viewModel.sectionIndex = SectionIndex();
        viewModel.sectionCount = editor_.graph.edges.at(editor_.selectedEdge).pluginSections.SpanCount();
    }
    for (const std::string& folder : CurrentFolders()) {
        TrackPluginSetItem item;
        item.folderUtf8 = folder;
        const TrackPluginEntry* found = selector_.FindTrackPluginEntry(folder);
        if (found != nullptr) {
            item.loaded = true;
            item.type = found->type;
            item.displayName = found->displayName;
            item.discrete = IsDiscreteRailPlacementType(found->type);
            item.intervalMeters = found->def.pier.interval > 0.0f ? found->def.pier.interval
                                                                  : kDefaultPierIntervalMeters;
        }
        viewModel.items.push_back(std::move(item));
    }
    return viewModel;
}

bool TrackPluginSetAction::Contains(const std::string& folderUtf8) const {
    return ContainsPath(CurrentFolders(), folderUtf8);
}

TrackPluginSetResult TrackPluginSetAction::Add(const std::string& folderUtf8) {
    TrackPluginSetResult result;
    std::string error;
    const std::optional<std::string> registered = selector_.EnsureTrackPluginLoaded(folderUtf8, error);
    if (!registered) {
        result.status = TrackPluginSetStatus::LoadFailed;
        result.detail = error;
        return result;
    }
    std::vector<std::string> folders = CurrentFolders();
    if (ContainsPath(folders, *registered)) return result;
    folders.push_back(*registered);
    if (!ReplaceFolders(std::move(folders))) result.status = TrackPluginSetStatus::RejectedMaterialized;
    return result;
}

TrackPluginSetResult TrackPluginSetAction::Remove(const std::string& folderUtf8) {
    TrackPluginSetResult result;
    std::vector<std::string> folders = CurrentFolders();
    const std::size_t before = folders.size();
    std::erase_if(folders, [&folderUtf8](const std::string& folder) { return SamePluginPath(folder, folderUtf8); });
    if (folders.size() == before) return result;
    if (!ReplaceFolders(std::move(folders))) result.status = TrackPluginSetStatus::RejectedMaterialized;
    return result;
}

TrackPluginSetResult TrackPluginSetAction::ApplyTemplateFile(const std::string& pathUtf8) {
    TrackPluginSetResult result;
    const RailwayPluginSetApplyResult applied = ApplyRailwayPluginSetFile(pathUtf8, selector_);
    std::vector<std::string> folders = CurrentFolders();
    bool anyAdded = false;
    for (const std::string& folder : applied.resolvedFolders) {
        if (ContainsPath(folders, folder)) continue; // 同じ読込内の重複もここで除かれる
        folders.push_back(folder);
        anyAdded = true;
    }
    if (!applied.ok) result.status = TrackPluginSetStatus::TemplateLoadFailed;
    if (anyAdded && !ReplaceFolders(std::move(folders))) result.status = TrackPluginSetStatus::RejectedMaterialized;
    return result;
}

} // namespace rs::app
