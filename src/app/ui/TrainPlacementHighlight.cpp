//
// TrainPlacementHighlight.cpp
//
#include "TrainPlacementHighlight.h"

#include "OverlayLineTuning.h"

#include "../../core/DebugDraw.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../gpu/App.h"
#include "OverlayColorTuning.h"
#include "../../editor/tool/Editor.h"

namespace rs::app {

void DrawTrainPlacementHighlight(const EditorViewportContext& ctx) {
    const TrainPlacementPreview& preview = ctx.trainPlacementUi.preview;
    if (!preview.valid) return;
    const rs::world::TrackGraph& graph = ctx.editor.graph;
    const std::unordered_map<rs::world::Guid, rs::world::TrackEdge,
                             rs::world::GuidHash>::const_iterator it = graph.edges.find(preview.head.edge);
    if (it == graph.edges.end()) return;

    const rs::world::TrackFrame frame =
        rs::world::EvaluateAt(it->second, preview.head.s, preview.head.direction);
    const rs::core::Vec3 headPoint = frame.pos + frame.up * (it->second.railTopLift + OverlayLineTuning::Embedded().trainPlacementLiftM);
    const rs::core::Vec3 right = rs::core::Vec3::Cross(frame.up, frame.tangent).Normalized();

    rs::core::DebugStyle style;
    // 置ける=成立の色、置けない=不成立の色。
    const OverlayColorTuning& colors = OverlayColorTuning::Embedded();
    style.color = preview.blocked ? colors.invalid : colors.valid;
    style.widthPx = OverlayLineTuning::Embedded().trainPlacementLineWidthPx;
    ctx.app.DebugDrawList().AddLine(headPoint - right * OverlayLineTuning::Embedded().trainPlacementHeadBarHalfWidthM,
                                    headPoint + right * OverlayLineTuning::Embedded().trainPlacementHeadBarHalfWidthM, style);
}

} // namespace rs::app
