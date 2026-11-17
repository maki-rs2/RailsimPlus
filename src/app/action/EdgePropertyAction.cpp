//
// EdgePropertyAction.cpp - 選択edgeプロパティのアプリサービス
//
#include "EdgePropertyAction.h"
#include "../../editor/tool/Editor.h"

#include <algorithm>
#include <cmath>

#include "TrackLayingText.h"

#include "../plugin_session/LayoutSession.h"
#include "../plugin_session/PluginSelector.h"
#include "../../world/alignment/Alignment.h"

namespace rs::app {
namespace {

bool HasTransitionSegment(const rs::world::TrackEdge& edge) {
    for (const rs::world::AlignmentSegment& segment : edge.curve) {
        if (segment.kind == rs::world::AlignmentSegment::Kind::Transition) {
            return true;
        }
    }
    return false;
}

int PendingPlacementEditCount(const rs::editor::Editor& editor,
                              const rs::world::Guid& edgeGuid) {
    int count = 0;
    for (const rs::world::PlacedIndividual& pole : editor.manualOverlay.poles) {
        if (pole.edge == edgeGuid) {
            ++count;
        }
    }
    for (const rs::world::PlacedIndividual& pier : editor.manualOverlay.piers) {
        if (pier.edge == edgeGuid) {
            ++count;
        }
    }
    return count;
}

} // namespace

EdgePropertyAction::EdgePropertyAction(rs::editor::Editor& editor, LayoutSession& layoutSession,
                                       const PluginSelector& selector)
    : editor_(editor), layoutSession_(layoutSession), selector_(selector) {}

void EdgePropertyAction::SetSpeedLimit(float speedLimitKmh) {
    editor_.SetSelectedEdgesProperty(
        [speedLimitKmh](const rs::world::TrackEdge&) -> std::optional<rs::editor::EdgePropertyDelta> {
            rs::editor::EdgePropertyDelta delta;
            delta.speedLimitKmh = speedLimitKmh;
            return delta;
        });
}

void EdgePropertyAction::SetCant(float cantA, float cantB, std::optional<float> cantMid,
                                 std::optional<float> cantTransitionRatio) {
    rs::editor::EdgePropertyDelta delta;
    delta.cantA = cantA;
    delta.cantB = cantB;
    delta.cantMid = cantMid;
    if (cantTransitionRatio.has_value()) {
        delta.cantTransitionRatio = std::max(*cantTransitionRatio, rs::world::TrackEdge::kMinCantTransitionRatio);
    }
    // 値を手で指定したら、取り込んだ制御点が上書きしないよう台形/線形へ戻す。
    delta.cantPoints = std::vector<rs::world::CantControlPoint>{};
    editor_.SetSelectedEdgeProperty(delta);
}

void EdgePropertyAction::SetCantAuto(bool enabled) {
    editor_.SetSelectedEdgesProperty(
        [enabled](const rs::world::TrackEdge& edge) -> std::optional<rs::editor::EdgePropertyDelta> {
            if (edge.cantAuto == enabled) return std::nullopt;
            rs::editor::EdgePropertyDelta delta;
            delta.cantAuto = enabled;
            return delta;
        });
}

void EdgePropertyAction::SetCantAutoMax(float cantAutoMax) {
    const float value = std::max(cantAutoMax, rs::world::TrackEdge::kMinCantAutoMax);
    editor_.SetSelectedEdgesProperty(
        [value](const rs::world::TrackEdge& edge) -> std::optional<rs::editor::EdgePropertyDelta> {
            if (edge.cantAutoMax == value) return std::nullopt;
            rs::editor::EdgePropertyDelta delta;
            delta.cantAutoMax = value;
            return delta;
        });
}

void EdgePropertyAction::EnableCantAuto() {
    editor_.SetSelectedEdgesProperty(
        [](const rs::world::TrackEdge& edge) -> std::optional<rs::editor::EdgePropertyDelta> {
            if (edge.cantAuto) return std::nullopt;
            // 自動に切り替えた直後にカントが消えたように見えないよう、今のカント(絶対値の最大)を
            // 最大カントの初期値にする。カントが全て0なら、保存されている最大カントのまま。
            float currentMax = std::max(std::fabs(edge.cantA), std::fabs(edge.cantB));
            if (HasTransitionSegment(edge) || edge.cantTransitionRatio > 0.0f) {
                currentMax = std::max(currentMax, std::fabs(edge.cantMid));
            }
            rs::editor::EdgePropertyDelta delta;
            delta.cantAuto = true;
            delta.cantAutoMax = currentMax > 0.0f ? currentMax : edge.cantAutoMax;
            return delta;
        });
}

void EdgePropertyAction::SetTransition(int comboIndex, float length) {
    const auto edgeIt = editor_.graph.edges.find(editor_.selectedEdge);
    if (edgeIt == editor_.graph.edges.end()) {
        return;
    }

    const TransitionChoice& choice = TransitionChoiceAt(comboIndex);
    const rs::world::AlignmentSegment::TransitionShape shape =
        choice.enabled ? choice.shape : edgeIt->second.refit.shape;
    float transitionLength = choice.enabled ? rs::editor::TrackLayingSettings::ClampTransitionLength(length) : 0.0f;
    if (choice.enabled && transitionLength <= 0.0f) {
        transitionLength = 20.0f;
    }
    editor_.SetSelectedEdgeTransition(shape, transitionLength);
}

void EdgePropertyAction::RemoveSelectedEdge() {
    editor_.RemoveSelectedEdge(&layoutSession_.warpLinks, layoutSession_.activeIndex);
}

void EdgePropertyAction::ClearPlacementEdits() {
    editor_.ClearPlacementEditsForEdge(editor_.selectedEdge);
}

EdgePropertyViewModel EdgePropertyAction::ViewModel() const {
    EdgePropertyViewModel viewModel;
    const auto edgeIt = editor_.graph.edges.find(editor_.selectedEdge);
    if (edgeIt == editor_.graph.edges.end()) {
        return viewModel;
    }

    // 複数選択のうち、速度制限やカントを持つ実体のレール(複線中心線を除く)。
    std::vector<const rs::world::TrackEdge*> realEdges;
    for (const rs::world::Guid& guid : editor_.SelectedEdges()) {
        const auto found = editor_.graph.edges.find(guid);
        if (found != editor_.graph.edges.end() && !found->second.centerlineOnly)
            realEdges.push_back(&found->second);
    }
    const bool multi = editor_.SelectedEdgeCount() > 1;
    // 値を読むレール。複数選択のときは、最後に選んだ実体のレール(中心線だけなら主のまま)。
    const rs::world::TrackEdge& edge = (multi && !realEdges.empty()) ? *realEdges.back() : edgeIt->second;
    viewModel.visible = true;
    viewModel.selectedCount = editor_.SelectedEdgeCount();
    viewModel.edgeGuid = editor_.selectedEdge;
    viewModel.isCenterline = edge.centerlineOnly;
    viewModel.trackCount = edge.trackCount;
    viewModel.memberEdgeCount = edge.trackEdges.size();
    viewModel.hasTransitionPlan = edge.refit.available;
    viewModel.transitionComboIndex = TransitionComboIndexOf(edge.refit.transitionLength > 0.0f, edge.refit.shape);
    viewModel.transitionLength = edge.refit.transitionLength;
    if (viewModel.isCenterline) {
        return viewModel;
    }

    viewModel.speedLimit = edge.speedLimitKmh;
    viewModel.cantA = edge.cantA;
    viewModel.cantB = edge.cantB;
    viewModel.hasCantMid = HasTransitionSegment(edge) || edge.cantTransitionRatio > 0.0f;
    viewModel.cantMid = edge.cantMid;
    viewModel.cantTransitionRatio = edge.cantTransitionRatio;
    viewModel.cantAuto = edge.cantAuto;
    viewModel.cantAutoMax = edge.cantAutoMax;
    if (multi) {
        std::size_t autoCount = 0;
        for (const rs::world::TrackEdge* other : realEdges) {
            if (other->speedLimitKmh != edge.speedLimitKmh) viewModel.speedLimitMixed = true;
            if (other->cantAutoMax != edge.cantAutoMax) viewModel.cantAutoMaxMixed = true;
            if (other->cantAuto) ++autoCount;
        }
        viewModel.cantAuto = autoCount == realEdges.size();
        viewModel.cantAutoMixed = autoCount != 0 && autoCount != realEdges.size();
    }
    // 区間の選択は主の1本に対するものなので、複数選択のときは先頭の区間から軌間を引く。
    const std::size_t pluginSetIndex =
        multi ? 0 : editor_.SelectedSectionIndex(rs::editor::SectionLayer::PluginSet);
    const std::vector<rs::world::IntervalList<rs::world::PluginSet>::Span>& pluginSets =
        edge.pluginSections.Spans();
    if (pluginSetIndex < pluginSets.size()) {
        for (const std::string& folderUtf8 : pluginSets[pluginSetIndex].value.plugins) {
            const TrackPluginEntry* entry = selector_.FindTrackPluginEntry(folderUtf8);
            if (entry != nullptr && entry->type == rs::legacy_pi::PluginType::Rail &&
                entry->def.gauge > 0.0f) {
                viewModel.gaugeMm = entry->def.gauge * 1000.0f;
                break;
            }
        }
    }
    viewModel.edgeLength = rs::world::AlignmentTotalLength(edge.curve);
    viewModel.pendingPlacementEdits = PendingPlacementEditCount(editor_, editor_.selectedEdge);
    return viewModel;
}

} // namespace rs::app
