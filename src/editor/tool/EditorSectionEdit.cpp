//
// EditorSectionEdit.cpp - EditorSectionEdit.h の実装
//
#include "EditorSectionEdit.h"
#include "Editor.h"

#include <algorithm>
#include <utility>

#include "../../world/alignment/TrackTraversal.h"
#include "EditorEdgeProperties.h"

namespace rs::editor {

using rs::world::Guid;
using rs::world::NilGuid;
using rs::world::PluginSet;
using rs::world::TrackEdge;

namespace {

// spans[boundaryIndex]の境界を取り除き、前の区間へ吸収した新しいIntervalListを
// 組み立てる(IntervalListは要素削除APIを持たないため、SplitAt+SetSpanValueで
// 再構築する)。boundaryIndexが1未満/範囲外の呼び出しは行わない前提(呼び出し側で
// 事前チェック済み)。
template <class T>
rs::world::IntervalList<T> RemoveBoundary(const rs::world::IntervalList<T>& src,
                                          std::size_t boundaryIndex) {
    const std::vector<typename rs::world::IntervalList<T>::Span>& spans = src.Spans();
    rs::world::IntervalList<T> result(spans.front().value);
    for (std::size_t i = 1; i < spans.size(); ++i) {
        if (i == boundaryIndex) continue; // この境界だけ落とす(前区間が吸収)
        if (result.SplitAt(spans[i].sStart)) {
            result.SetSpanValue(result.SpanCount() - 1, spans[i].value);
        }
    }
    return result;
}

// s を含む区間の添字を返す(始点包含)。IntervalList::ValueAt と同じ探索則。
template <class T>
std::size_t IndexAt(const rs::world::IntervalList<T>& list, double s) {
    const std::vector<typename rs::world::IntervalList<T>::Span>& spans = list.Spans();
    typename std::vector<typename rs::world::IntervalList<T>::Span>::const_iterator it =
        std::upper_bound(spans.begin(), spans.end(), s,
                         [](double value, const typename rs::world::IntervalList<T>::Span& span) {
                             return value < span.sStart;
                         });
    if (it == spans.begin()) return 0;
    --it;
    return static_cast<std::size_t>(it - spans.begin());
}

} // namespace

bool EditorSectionEdit::SplitSectionAt(Editor& editor,
                                       const Guid& edge, SectionLayer layer, double s) {
    rs::world::TrackGraph& graph = editor.graph;
    if (!(edge != NilGuid())) return false;
    const auto it = graph.edges.find(edge);
    if (it == graph.edges.end()) return false;
    const TrackEdge& e = it->second;
    const double totalLen = rs::world::EdgeLength(e);
    if (s <= 0.0 || s >= totalLen) return false;

    EdgePropertyDelta delta;
    switch (layer) {
        case SectionLayer::PluginSet: {
            rs::world::IntervalList<PluginSet> runAnchors = e.pluginSections;
            if (!runAnchors.SplitAt(s)) return false;
            delta.pluginSections = std::move(runAnchors);
            break;
        }
    }

    EditorEdgeProperties edgeProps;
    return edgeProps.SetSelectedEdgeProperty(editor, edge, delta);
}

bool EditorSectionEdit::RemoveSectionBoundary(Editor& editor,
                                              const Guid& edge, SectionLayer layer,
                                              std::size_t boundaryIndex) {
    rs::world::TrackGraph& graph = editor.graph;
    if (!(edge != NilGuid())) return false;
    if (boundaryIndex == 0) return false;
    const auto it = graph.edges.find(edge);
    if (it == graph.edges.end()) return false;
    const TrackEdge& e = it->second;

    EdgePropertyDelta delta;
    switch (layer) {
        case SectionLayer::PluginSet: {
            if (boundaryIndex >= e.pluginSections.SpanCount()) return false;
            delta.pluginSections = RemoveBoundary(e.pluginSections, boundaryIndex);
            break;
        }
    }

    EditorEdgeProperties edgeProps;
    return edgeProps.SetSelectedEdgeProperty(editor, edge, delta);
}

bool EditorSectionEdit::MoveSectionBoundary(Editor& editor,
                                            const Guid& edge, SectionLayer layer,
                                            std::size_t boundaryIndex, double newS) {
    rs::world::TrackGraph& graph = editor.graph;
    if (!(edge != NilGuid())) return false;
    const auto it = graph.edges.find(edge);
    if (it == graph.edges.end()) return false;
    const TrackEdge& e = it->second;

    // IntervalList は edge 全長を持っていないため、最後の境界は上限なしで動いてしまう。
    // edge の外へ出た境界は「edge 外にしか存在しない区間」を生むので、ここで
    // 全長側からもクランプする(末尾区間に kMinSpanLen の幅を残す)。
    const double totalLen = rs::world::EdgeLength(e);
    if (totalLen > 0.0) {
        newS = std::clamp(newS, 0.0, totalLen - rs::world::kMinSpanLen);
    }

    EdgePropertyDelta delta;
    switch (layer) {
        case SectionLayer::PluginSet: {
            rs::world::IntervalList<PluginSet> runAnchors = e.pluginSections;
            if (!runAnchors.SetSpanStart(boundaryIndex, newS)) return false;
            delta.pluginSections = std::move(runAnchors);
            break;
        }
    }

    EditorEdgeProperties edgeProps;
    return edgeProps.SetSelectedEdgeProperty(editor, edge, delta);
}

bool EditorSectionEdit::SetSectionFolders(Editor& editor,
                                          const Guid& edge, std::size_t sectionIndex,
                                          std::vector<std::string> folders,
                                          const rs::world::ManualPlacementOverlay& overlay) {
    rs::world::TrackGraph& graph = editor.graph;
    if (!(edge != NilGuid())) return false;
    const auto it = graph.edges.find(edge);
    if (it == graph.edges.end()) return false;
    const TrackEdge& e = it->second;
    if (sectionIndex >= e.pluginSections.SpanCount()) return false;

    const std::vector<std::string>& currentFolders =
        e.pluginSections.Spans()[sectionIndex].value.plugins;
    for (const std::string& folder : folders) {
        if (std::find(currentFolders.begin(), currentFolders.end(), folder) != currentFolders.end())
            continue;
        if (rs::world::FindRunAnchor(overlay.runAnchors, edge, folder) != nullptr)
            return false;
    }

    rs::world::IntervalList<PluginSet> runAnchors = e.pluginSections;
    PluginSet value;
    value.plugins = std::move(folders);
    runAnchors.SetSpanValue(sectionIndex, value);

    EdgePropertyDelta delta;
    delta.pluginSections = std::move(runAnchors);
    EditorEdgeProperties edgeProps;
    return edgeProps.SetSelectedEdgeProperty(editor, edge, delta);
}

bool EditorSectionEdit::EditFoldersOnEdges(Editor& editor,
                                           const std::vector<Guid>& edges,
                                           const std::vector<std::string>& add,
                                           const std::vector<std::string>& remove,
                                           const rs::world::ManualPlacementOverlay& overlay) {
    rs::world::TrackGraph& graph = editor.graph;
    const auto has = [](const std::vector<std::string>& folders, const std::string& folder) {
        return std::find(folders.begin(), folders.end(), folder) != folders.end();
    };
    // 先に全edgeを確かめ、1本でも拒否なら何も変えない。
    for (const Guid& guid : edges) {
        const auto it = graph.edges.find(guid);
        if (it == graph.edges.end() || it->second.centerlineOnly) continue;
        for (const auto& span : it->second.pluginSections.Spans()) {
            for (const std::string& folder : add) {
                if (!has(span.value.plugins, folder) &&
                    rs::world::FindRunAnchor(overlay.runAnchors, guid, folder) != nullptr)
                    return false;
            }
        }
    }

    EditorEdgeProperties edgeProps;
    return edgeProps.SetEdgesProperty(
        editor, edges,
        [&](const TrackEdge& edge) -> std::optional<EdgePropertyDelta> {
            if (edge.centerlineOnly) return std::nullopt;
            rs::world::IntervalList<PluginSet> sections = edge.pluginSections;
            bool changed = false;
            for (std::size_t i = 0; i < sections.SpanCount(); ++i) {
                PluginSet value = sections.Spans()[i].value;
                const std::size_t before = value.plugins.size();
                std::erase_if(value.plugins, [&](const std::string& folder) {
                    return has(remove, folder);
                });
                bool spanChanged = value.plugins.size() != before;
                for (const std::string& folder : add) {
                    if (has(value.plugins, folder)) continue;
                    value.plugins.push_back(folder);
                    spanChanged = true;
                }
                if (!spanChanged) continue;
                sections.SetSpanValue(i, value);
                changed = true;
            }
            if (!changed) return std::nullopt;
            EdgePropertyDelta delta;
            delta.pluginSections = std::move(sections);
            return delta;
        });
}

std::size_t EditorSectionEdit::SectionIndexAt(const TrackEdge& edge, SectionLayer layer,
                                              double s) {
    switch (layer) {
        case SectionLayer::PluginSet: return IndexAt(edge.pluginSections, s);
    }
    return 0;
}

std::size_t EditorSectionEdit::SectionCount(const TrackEdge& edge, SectionLayer layer) {
    switch (layer) {
        case SectionLayer::PluginSet: return edge.pluginSections.SpanCount();
    }
    return 0;
}

namespace {

template <class T>
std::vector<EditorSectionEdit::BoundaryS> BoundariesOf(const rs::world::IntervalList<T>& list) {
    std::vector<EditorSectionEdit::BoundaryS> result;
    const std::vector<typename rs::world::IntervalList<T>::Span>& spans = list.Spans();
    result.reserve(spans.size() > 0 ? spans.size() - 1 : 0);
    for (std::size_t i = 1; i < spans.size(); ++i) {
        result.push_back(EditorSectionEdit::BoundaryS{ i, spans[i].sStart });
    }
    return result;
}

} // namespace

std::vector<EditorSectionEdit::BoundaryS> EditorSectionEdit::CollectBoundaries(
    const TrackEdge& edge, SectionLayer layer) {
    switch (layer) {
        case SectionLayer::PluginSet: return BoundariesOf(edge.pluginSections);
    }
    return {};
}

} // namespace rs::editor
