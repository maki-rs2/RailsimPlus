//
// EditorEdgeProperties.cpp - EditorEdgeProperties.h の実装
//
#include "EditorEdgeProperties.h"
#include "Editor.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include "../../world/alignment/EdgeShapeBuild.h"
#include "PlacementRescaleCommand.h"

namespace rs::editor {

using rs::core::Vec3;
using rs::world::Guid;
using rs::world::NilGuid;
using rs::world::TrackEdge;

bool EditorEdgeProperties::SetSelectedEdgeProperty(Editor& editor,
                                                    const Guid& selectedEdge,
                                                    const EdgePropertyDelta& delta,
                                                    rs::world::ManualPlacementOverlay* overlay) {
    rs::world::TrackGraph& graph = editor.graph;
    if (!(selectedEdge != NilGuid())) return false;
    if (graph.edges.find(selectedEdge) == graph.edges.end()) return false;
    return ExecuteWithPlacementRescale(editor, overlay,
                                std::make_unique<SetEdgePropertyCommand>(selectedEdge, delta));
}

bool EditorEdgeProperties::SetEdgesProperty(Editor& editor,
                                            const std::vector<Guid>& edges,
                                            const EdgeDeltaFn& makeDelta,
                                            rs::world::ManualPlacementOverlay* overlay) {
    rs::world::TrackGraph& graph = editor.graph;
    CompositeCommand composite;
    for (const Guid& guid : edges) {
        const auto it = graph.edges.find(guid);
        if (it == graph.edges.end()) continue;
        std::optional<EdgePropertyDelta> delta = makeDelta(it->second);
        if (!delta) continue;
        composite.Add(std::make_unique<SetEdgePropertyCommand>(guid, std::move(*delta)));
    }
    if (composite.Empty()) return false;
    return ExecuteWithPlacementRescale(editor, overlay,
                                std::make_unique<CompositeCommand>(std::move(composite)));
}

bool EditorEdgeProperties::ToggleSelectedEdgeTrackPlugin(Editor& editor,
                                                          const Guid& selectedEdge,
                                                          const std::string& folderUtf8) {
    rs::world::TrackGraph& graph = editor.graph;
    if (!(selectedEdge != NilGuid())) return false;
    const auto it = graph.edges.find(selectedEdge);
    if (it == graph.edges.end()) return false;
    const TrackEdge& edge = it->second;
    const bool has = rs::world::EdgeHasFolder(edge, folderUtf8);

    // 全区間に対してfolderUtf8を追加/削除する(全区間に既に含まれていれば全区間
    // から削除、そうでなければ全区間へ追加。重複追加はしない)。境界は動かさない
    // ので値だけをSetSpanValueで差し替える。
    rs::world::IntervalList<rs::world::PluginSet> sections = edge.pluginSections;
    for (size_t i = 0; i < sections.SpanCount(); ++i) {
        rs::world::PluginSet value = sections.Spans()[i].value;
        const auto pos = std::find(value.plugins.begin(), value.plugins.end(), folderUtf8);
        if (has) {
            if (pos != value.plugins.end()) value.plugins.erase(pos);
        } else {
            if (pos == value.plugins.end()) value.plugins.push_back(folderUtf8);
        }
        sections.SetSpanValue(i, value);
    }

    EdgePropertyDelta delta;
    delta.pluginSections = std::move(sections);
    return SetSelectedEdgeProperty(editor, selectedEdge, delta);
}

// edge内プラグイン区間。既に所属済みのfolderUtf8が[sStart,sEnd]の
// 範囲でだけ有効になるよう、edgeの全区間を書き換える(未所属なら何もしない、
// 範囲指定は「追加時のオプション」でありトグルの代わりにはしない)。sEnd<0は
// 「edge終端まで」。他のフォルダは各区間でそのまま維持する。
bool EditorEdgeProperties::SetSelectedEdgeFolderRange(Editor& editor,
                                                       const Guid& selectedEdge,
                                                       const std::string& folderUtf8,
                                                       double sStart, double sEnd) {
    rs::world::TrackGraph& graph = editor.graph;
    if (!(selectedEdge != NilGuid())) return false;
    const auto it = graph.edges.find(selectedEdge);
    if (it == graph.edges.end()) return false;
    const TrackEdge& edge = it->second;
    if (!rs::world::EdgeHasFolder(edge, folderUtf8)) return false;

    const double totalLen =
        edge.spline.empty() ? 0.0 : static_cast<double>(edge.spline.back().s);
    const double s0 = std::clamp(sStart, 0.0, totalLen);
    const double s1 = sEnd < 0.0 ? totalLen : std::clamp(sEnd, 0.0, totalLen);

    rs::world::IntervalList<rs::world::PluginSet> sections = edge.pluginSections;
    sections.SplitAt(s0);
    // s1がedge終端そのものなら境界を置かない(終端はIntervalListが持たない暗黙の
    // 終わりなので、ここで境界を置くとedge外に空区間が生える)。
    if (s1 < totalLen - rs::world::kMinSpanLen) sections.SplitAt(s1);

    // 境界が確定したので、各区間の値だけを差し替える(範囲内なら含める、範囲外なら
    // 外す。他のフォルダは各区間でそのまま維持する)。
    for (size_t i = 0; i < sections.SpanCount(); ++i) {
        const double spanStart = sections.Spans()[i].sStart;
        // s0/s1が既存境界へスナップされた場合に備えて許容差を持たせる。
        const bool within = spanStart >= s0 - rs::world::kMinSpanLen &&
                            spanStart < s1 - rs::world::kMinSpanLen;
        rs::world::PluginSet value = sections.Spans()[i].value;
        const auto pos = std::find(value.plugins.begin(), value.plugins.end(), folderUtf8);
        const bool spanHas = pos != value.plugins.end();
        if (within && !spanHas) {
            value.plugins.push_back(folderUtf8);
        } else if (!within && spanHas) {
            value.plugins.erase(pos);
        }
        sections.SetSpanValue(i, value);
    }

    EdgePropertyDelta delta;
    delta.pluginSections = std::move(sections);
    return SetSelectedEdgeProperty(editor, selectedEdge, delta);
}

namespace {

// edge に refit を与えたときの形を、現在の両端ノードの姿勢から作れるか。
rs::world::EdgeShapeBuild BuildShapeWithRefit(const rs::world::TrackGraph& graph,
                                              const TrackEdge& edge,
                                              const TrackEdge::Refit& refit) {
    const auto itA = graph.nodes.find(edge.nodeA);
    const auto itB = graph.nodes.find(edge.nodeB);
    if (itA == graph.nodes.end() || itB == graph.nodes.end()) return {};
    TrackEdge candidate = edge;
    candidate.refit = refit;
    return rs::world::BuildEdgeShape(
        rs::world::MakeEdgeShapeInput(candidate, itA->second, itB->second));
}

}  // namespace

bool EditorEdgeProperties::SetSelectedEdgeTransition(
    Editor& editor,
    rs::world::ManualPlacementOverlay& overlay, const Guid& selectedEdge,
    rs::world::AlignmentSegment::TransitionShape shape, float newTransLen) {
    rs::world::TrackGraph& graph = editor.graph;
    if (!(selectedEdge != NilGuid())) return false;
    auto eit = graph.edges.find(selectedEdge);
    if (eit == graph.edges.end()) return false;
    const TrackEdge e = eit->second; // 後段でtrackEdgesを辿りつつ同一edgeを間接参照するためコピーしておく
    if (!e.refit.available) return false; // 3点フィレット由来でない(biarc/.rs2取込/直線)

    // 形は緩和曲線の指定(refit)と両端ノードの姿勢から作り直される。curve は書かない。
    TrackEdge::Refit refit;
    refit.shape = shape;
    refit.transitionLength = std::max(newTransLen, 0.0f);
    refit.available = true;
    rs::world::EdgeShapeBuild built = BuildShapeWithRefit(graph, e, refit);
    if (!built.ok) return false;
    // 保存する長さは、幾何学的な最大長で頭打ちになった実効長(表示と再構築の入力を一致させる)。
    if (refit.transitionLength > 0.0f && built.effectiveTransitionLength != refit.transitionLength) {
        refit.transitionLength = built.effectiveTransitionLength;
        if (!BuildShapeWithRefit(graph, e, refit).ok) return false;
    }

    CompositeCommand composite;
    EdgePropertyDelta delta;
    delta.refit = refit;
    composite.Add(std::make_unique<SetEdgePropertyCommand>(selectedEdge, delta));

    // 中心線(centerlineOnly)なら、独立編集済み(freeze)でないmemberにも同じ指定を与える。
    // memberは自分のノード位置・接線のまま、指定だけを変えて姿勢から作り直す。
    // 緩和曲線の指定が変わった edge の事前確認はmemberも含めて全件行い、1本でも
    // 解けなければ全体を確定しない(生成時と同じ方針)。
    if (e.centerlineOnly) {
        for (const Guid& memberGuid : e.trackEdges) {
            const auto mIt = graph.edges.find(memberGuid);
            if (mIt == graph.edges.end()) return false;
            const TrackEdge& m = mIt->second;
            if (m.detachedFromCenterline) continue; // 独立編集済みは追従対象外
            if (!m.refit.available) continue;
            if (!BuildShapeWithRefit(graph, m, refit).ok) return false;
            EdgePropertyDelta mdelta;
            mdelta.refit = refit;
            composite.Add(std::make_unique<SetEdgePropertyCommand>(memberGuid, mdelta));
        }
    }

    return ExecuteWithPlacementRescale(editor, &overlay,
                                std::make_unique<CompositeCommand>(std::move(composite)));
}

bool EditorEdgeProperties::TrySetPointActiveLink(Editor& editor, const Guid& nodeGuid,
                                                 int newActiveIndex) {
    rs::world::TrackGraph& graph = editor.graph;
    const auto nit = graph.nodes.find(nodeGuid);
    if (nit == graph.nodes.end()) return false;
    const rs::world::TrackNode& node = nit->second;
    if (newActiveIndex < 0 || newActiveIndex >= static_cast<int>(node.links.size())) return false;
    const int side = node.links[static_cast<size_t>(newActiveIndex)].side;
    if (!node.IsBranching(side)) return false;  // その面は分岐していない(選択の余地が無い)
    if (node.IsLinkActive(newActiveIndex)) return false;  // 変更なし

    PointLinksDelta delta;
    delta.activeSlot = node.activeSlot;
    delta.activeSlot[static_cast<size_t>(side)] = node.SlotOfLink(newActiveIndex);
    return editor.ExecuteCommand(std::make_unique<SetPointLinksCommand>(nodeGuid, delta));
}

} // namespace rs::editor
