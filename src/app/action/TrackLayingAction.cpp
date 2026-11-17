//
// TrackLayingAction.cpp - 敷設ツール設定と敷設プレビュー表示のアプリサービス
//
#include "TrackLayingAction.h"
#include "../../editor/tool/Editor.h"

#include "TrackLayingText.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace rs::app {

TrackLayingAction::TrackLayingAction(rs::editor::Editor& editor,
                                     const rs::editor::PlacePreview& preview)
    : editor_(editor), preview_(preview) {}

void TrackLayingAction::SelectTool(bool placeMode) {
    const rs::editor::ToolMode mode =
        placeMode ? rs::editor::ToolMode::PlaceArc : rs::editor::ToolMode::Select;
    if (editor_.mode == mode) return;
    // ツール切替時は敷設途中の状態を完全に解除する。
    editor_.CancelPlacement();
    editor_.mode = mode;
    // 敷設面の高さの手動調整分は、ツールに入るときも出るときも0へ戻す。
    editor_.layingSettings.placeHeightOffset = 0.0f;
}

void TrackLayingAction::Undo() {
    editor_.Undo();
}

void TrackLayingAction::Redo() {
    editor_.Redo();
}

void TrackLayingAction::SetAngleSnapEnabled(bool enabled) {
    editor_.SetLayingSetting(&rs::editor::TrackLayingSettings::angleSnapEnabled, enabled);
}

void TrackLayingAction::SetAngleSnapDegrees(float degrees) {
    editor_.SetLayingSetting(&rs::editor::TrackLayingSettings::angleSnapDeg,
                             rs::editor::TrackLayingSettings::ClampAngleSnapDegrees(degrees));
}

void TrackLayingAction::SetTrackCount(int count) {
    editor_.SetLayingSetting(&rs::editor::TrackLayingSettings::trackCount,
                             rs::editor::TrackLayingSettings::ClampTrackCount(count));
}

void TrackLayingAction::SetTrackSpacing(float spacing) {
    editor_.SetLayingSetting(&rs::editor::TrackLayingSettings::trackSpacing,
                             rs::editor::TrackLayingSettings::ClampTrackSpacing(spacing));
}

void TrackLayingAction::SetTransition(bool enabled,
                                      rs::world::AlignmentSegment::TransitionShape shape) {
    editor_.SetLayingSetting(&rs::editor::TrackLayingSettings::transEnabled, enabled);
    if (enabled) editor_.SetLayingSetting(&rs::editor::TrackLayingSettings::transShape, shape);
}

void TrackLayingAction::SetTransitionLength(float length) {
    editor_.SetLayingSetting(&rs::editor::TrackLayingSettings::transLen,
                             rs::editor::TrackLayingSettings::ClampTransitionLength(length));
}

void TrackLayingAction::SetPlacementAddFolder(std::string folderUtf8) {
    editor_.placementAddFolder = std::move(folderUtf8);
}

void TrackLayingAction::SetPlacementAddMode(bool enabled) {
    editor_.placementAddMode = enabled;
}

void TrackLayingAction::SetWireEditMode(bool enabled) {
    editor_.wireEditMode = enabled;
}

TrackLayingViewModel TrackLayingAction::ViewModel() const {
    TrackLayingViewModel viewModel;
    viewModel.placeMode = editor_.mode == rs::editor::ToolMode::PlaceArc;
    viewModel.canUndo = editor_.CanUndo();
    viewModel.canRedo = editor_.CanRedo();
    viewModel.nodeCount = editor_.graph.nodes.size();
    viewModel.edgeCount = editor_.graph.edges.size();

    viewModel.angleSnapEnabled = editor_.layingSettings.angleSnapEnabled;
    viewModel.angleSnapDegrees = editor_.layingSettings.angleSnapDeg;
    viewModel.trackCount = editor_.layingSettings.trackCount;
    viewModel.trackSpacing = editor_.layingSettings.trackSpacing;
    viewModel.transitionEnabled = editor_.layingSettings.transEnabled;
    viewModel.transitionComboIndex =
        TransitionComboIndexOf(viewModel.transitionEnabled, editor_.layingSettings.transShape);
    viewModel.transitionLength = editor_.layingSettings.transLen;
    viewModel.placeHeight = editor_.layingSettings.placeHeight;

    viewModel.sideRejected = preview_.sideRejected;
    viewModel.sideRejectReason = preview_.sideRejectReason;
    viewModel.commitRejectReason = editor_.CommitRejectReason();
    viewModel.selectedStartNodeCount = editor_.SelectedStartNodes().size();
    viewModel.startSelectionRejected = editor_.StartSelectionRejected();
    viewModel.startSelectionRejectReason = editor_.StartSelectionRejectReason();
    viewModel.showEndNodeProgress = editor_.ExistingNodeStart() && editor_.ActiveTrackCount() > 1;
    viewModel.selectedEndNodeCount = editor_.SelectedEndNodes().size();
    viewModel.activeTrackCount = editor_.ActiveTrackCount();

    viewModel.placementAddMode = editor_.placementAddMode;
    viewModel.placementAddFolder = editor_.placementAddFolder;
    viewModel.wireEditMode = editor_.wireEditMode;

    // 3点フィレットの幾何拘束で指定緩和長が入りきらないとき、この形状での上限を出す。
    if (preview_.haveSolve && preview_.solve.acceptable && preview_.solve.transLen > 0.0f &&
        preview_.solve.transLen < viewModel.transitionLength - 0.05f) {
        viewModel.transitionShortened = true;
        viewModel.effectiveTransitionLength = preview_.solve.transLen;
    }

    ArcPreviewViewModel& preview = viewModel.preview;
    if (!viewModel.placeMode || !preview_.active) return viewModel;
    if (preview_.awaitingMidpoint) {
        preview.kind = ArcPreviewKind::AwaitingMidpoint;
        return viewModel;
    }
    if (preview_.endSnapUnreachable) {
        preview.kind = ArcPreviewKind::EndSnapUnreachable;
        return viewModel;
    }
    if (!preview_.haveSolve) return viewModel;
    if (!preview_.solve.acceptable) {
        preview.kind = ArcPreviewKind::Rejected;
        preview.rejectReason = preview_.solve.rejectReason;
        return viewModel;
    }

    const rs::world::ThreePointArcSolveResult& solve = preview_.solve;
    preview.kind = ArcPreviewKind::Solved;
    preview.isSCurve = preview_.endSnappedToNode && preview_.arcChords.size() == 2;
    preview.mode = preview.isSCurve          ? ArcPreviewMode::SCurve
                   : preview_.endSnappedToNode ? ArcPreviewMode::EndNodeSnap
                   : editor_.layingSettings.AngleSnapActive() ? ArcPreviewMode::AngleSnap
                                                : ArcPreviewMode::Free;
    preview.isStraightFallback = solve.isStraightFallback;
    preview.turnAngleDeg = solve.theta * 180.0f / std::numbers::pi_v<float>;
    preview.radius = solve.radius;
    preview.preLineLength = solve.preLineLen;
    preview.postLineLength = solve.postLineLen;
    preview.transitionLength = solve.transLen;
    if (preview.isStraightFallback) {
        preview.totalLength = solve.curve.empty() ? 0.0f : solve.curve[0].length;
    }
    if (preview.isSCurve) {
        preview.radius1 = std::fabs(preview_.arcChords[0].radius);
        preview.radius2 = std::fabs(preview_.arcChords[1].radius);
    }
    if (solve.transLen > 0.0f) {
        // 緩和曲線ぶんを除いた円弧の実長。
        for (const rs::world::AlignmentSegment& segment : solve.curve) {
            if (segment.kind == rs::world::AlignmentSegment::Kind::Arc) {
                preview.arcLength += segment.length;
            }
        }
    } else {
        preview.arcLength = std::fabs(solve.radius) * std::fabs(solve.theta);
    }
    return viewModel;
}

} // namespace rs::app
