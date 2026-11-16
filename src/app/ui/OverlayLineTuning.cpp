#include "OverlayLineTuning.h"

#include "../../core/ConfigJson.h"

#include <algorithm>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "EmbeddedOverlayLineTuning.h"

namespace rs::app {

namespace {

OverlayLineTuning ParseEmbedded() {
    OverlayLineTuning tuning = rs::core::ReadEmbeddedConfig<OverlayLineTuning>(
        rs::embedded::kOverlayLineTuningJson, rs::embedded::kOverlayLineTuningJsonSize, "OverlayLineTuning",
        [](const nlohmann::json& root) {
            OverlayLineTuning tuning;
            tuning.minWidthPx = root.value("minWidthPx", tuning.minWidthPx);
            tuning.selectedWidthPx = root.value("selectedWidthPx", tuning.selectedWidthPx);
            tuning.nodeLineWidthPx = root.value("nodeLineWidthPx", tuning.nodeLineWidthPx);
            tuning.guideLineWidthPx = root.value("guideLineWidthPx", tuning.guideLineWidthPx);
            tuning.sectionLineWidthPx = root.value("sectionLineWidthPx", tuning.sectionLineWidthPx);
            tuning.nodeRadiusPx = root.value("nodeRadiusPx", tuning.nodeRadiusPx);
            tuning.nodeHoverRadiusPx = root.value("nodeHoverRadiusPx", tuning.nodeHoverRadiusPx);
            tuning.selectRingRadiusPx = root.value("selectRingRadiusPx", tuning.selectRingRadiusPx);
            tuning.markerRadiusPx = root.value("markerRadiusPx", tuning.markerRadiusPx);
            tuning.dragTipRadiusPx = root.value("dragTipRadiusPx", tuning.dragTipRadiusPx);
            tuning.dotRadiusPx = root.value("dotRadiusPx", tuning.dotRadiusPx);
            tuning.dotHoverRadiusPx = root.value("dotHoverRadiusPx", tuning.dotHoverRadiusPx);
            tuning.snapCursorRadiusPx = root.value("snapCursorRadiusPx", tuning.snapCursorRadiusPx);
            tuning.heightFootRadiusPx = root.value("heightFootRadiusPx", tuning.heightFootRadiusPx);
            tuning.signalChevronSpacingM = root.value("signalChevronSpacingM", tuning.signalChevronSpacingM);
            tuning.signalRangeUnderlayExtraPx = root.value("signalRangeUnderlayExtraPx", tuning.signalRangeUnderlayExtraPx);
            tuning.signalMarkerHalfWidthM = root.value("signalMarkerHalfWidthM", tuning.signalMarkerHalfWidthM);
            tuning.signalArrowLengthM = root.value("signalArrowLengthM", tuning.signalArrowLengthM);
            tuning.signalArrowHeadM = root.value("signalArrowHeadM", tuning.signalArrowHeadM);
            tuning.wirePointPickRadiusPx = root.value("wirePointPickRadiusPx", tuning.wirePointPickRadiusPx);
            tuning.wireSpanPickRadiusPx = root.value("wireSpanPickRadiusPx", tuning.wireSpanPickRadiusPx);
            tuning.dragStartDistancePx = root.value("dragStartDistancePx", tuning.dragStartDistancePx);
            tuning.trainPlacementLineWidthPx = root.value("trainPlacementLineWidthPx", tuning.trainPlacementLineWidthPx);
            tuning.trainPlacementHeadBarHalfWidthM = root.value("trainPlacementHeadBarHalfWidthM", tuning.trainPlacementHeadBarHalfWidthM);
            tuning.trainPlacementLiftM = root.value("trainPlacementLiftM", tuning.trainPlacementLiftM);
            tuning.pointHandMarkDistanceM = root.value("pointHandMarkDistanceM", tuning.pointHandMarkDistanceM);
            return tuning;
        });
    tuning.minWidthPx = std::max(tuning.minWidthPx, 0.0f);
    tuning.selectedWidthPx = std::max(tuning.selectedWidthPx, tuning.minWidthPx);
    tuning.nodeLineWidthPx = std::max(tuning.nodeLineWidthPx, 0.0f);
    tuning.guideLineWidthPx = std::max(tuning.guideLineWidthPx, 0.0f);
    tuning.sectionLineWidthPx = std::max(tuning.sectionLineWidthPx, 0.0f);
    tuning.nodeRadiusPx = std::max(tuning.nodeRadiusPx, 0.0f);
    tuning.nodeHoverRadiusPx = std::max(tuning.nodeHoverRadiusPx, 0.0f);
    tuning.selectRingRadiusPx = std::max(tuning.selectRingRadiusPx, 0.0f);
    tuning.markerRadiusPx = std::max(tuning.markerRadiusPx, 0.0f);
    tuning.dragTipRadiusPx = std::max(tuning.dragTipRadiusPx, 0.0f);
    tuning.dotRadiusPx = std::max(tuning.dotRadiusPx, 0.0f);
    tuning.dotHoverRadiusPx = std::max(tuning.dotHoverRadiusPx, 0.0f);
    tuning.snapCursorRadiusPx = std::max(tuning.snapCursorRadiusPx, 0.0f);
    tuning.heightFootRadiusPx = std::max(tuning.heightFootRadiusPx, 0.0f);
    return tuning;
}

} // namespace

const OverlayLineTuning& OverlayLineTuning::Embedded() {
    static const OverlayLineTuning kTuning = ParseEmbedded();
    return kTuning;
}

} // namespace rs::app
