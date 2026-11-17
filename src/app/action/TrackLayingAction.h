#pragma once

#include <cstddef>
#include <string>

#include "../../editor/tool/EditorTypes.h"

namespace rs::editor { class Editor; }

namespace rs::app {

enum class ArcPreviewMode { SCurve, EndNodeSnap, AngleSnap, Free };
enum class ArcPreviewKind { None, AwaitingMidpoint, EndSnapUnreachable, Solved, Rejected };

struct ArcPreviewViewModel {
    ArcPreviewKind kind = ArcPreviewKind::None;
    ArcPreviewMode mode = ArcPreviewMode::Free;
    bool isStraightFallback = false;
    bool isSCurve = false;
    float totalLength = 0.0f;
    float turnAngleDeg = 0.0f;
    float radius = 0.0f;
    float radius1 = 0.0f;
    float radius2 = 0.0f;
    float preLineLength = 0.0f;
    float postLineLength = 0.0f;
    float transitionLength = 0.0f;
    float arcLength = 0.0f;
    const char* rejectReason = nullptr;
};

struct TrackLayingViewModel {
    bool placeMode = false;
    bool canUndo = false;
    bool canRedo = false;
    std::size_t nodeCount = 0;
    std::size_t edgeCount = 0;
    bool angleSnapEnabled = false;
    float angleSnapDegrees = 0.0f;
    int trackCount = 1;
    float trackSpacing = 0.0f;
    bool transitionEnabled = false;
    int transitionComboIndex = 0;
    float transitionLength = 0.0f;
    float placeHeight = 0.0f;
    bool transitionShortened = false;
    float effectiveTransitionLength = 0.0f;
    bool sideRejected = false;
    std::string sideRejectReason;
    std::string commitRejectReason;
    std::size_t selectedStartNodeCount = 0;
    bool startSelectionRejected = false;
    const char* startSelectionRejectReason = nullptr;
    bool showEndNodeProgress = false;
    std::size_t selectedEndNodeCount = 0;
    int activeTrackCount = 0;
    bool placementAddMode = false;
    std::string placementAddFolder;
    bool wireEditMode = false;
    ArcPreviewViewModel preview;
};

class TrackLayingAction {
public:
    TrackLayingAction(rs::editor::Editor& editor, const rs::editor::PlacePreview& preview);

    void SelectTool(bool placeMode);
    void Undo();
    void Redo();
    void SetAngleSnapEnabled(bool enabled);
    void SetAngleSnapDegrees(float degrees);
    void SetTrackCount(int count);
    void SetTrackSpacing(float spacing);
    void SetTransition(bool enabled, rs::world::AlignmentSegment::TransitionShape shape);
    void SetTransitionLength(float length);
    void SetPlacementAddFolder(std::string folderUtf8);
    void SetPlacementAddMode(bool enabled);
    void SetWireEditMode(bool enabled);
    TrackLayingViewModel ViewModel() const;

private:
    rs::editor::Editor& editor_;
    const rs::editor::PlacePreview& preview_;
};

} // namespace rs::app
