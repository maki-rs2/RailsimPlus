#include "BrowserTuning.h"

#include "../../core/ConfigJson.h"
#include "EmbeddedBrowserTuning.h"

namespace rs::app::plugin_browser {

const BrowserTuning& BrowserTuning::Embedded() {
    static const BrowserTuning kTuning = rs::core::ReadEmbeddedConfig<BrowserTuning>(
        rs::embedded::kBrowserTuningJson, rs::embedded::kBrowserTuningJsonSize, "BrowserTuning",
        [](const nlohmann::json& root) {
            BrowserTuning tuning;
            tuning.railThumbnailLengthM = root.value("railThumbnailLengthM", tuning.railThumbnailLengthM);
            tuning.girderThumbnailLengthM = root.value("girderThumbnailLengthM", tuning.girderThumbnailLengthM);
            tuning.lineThumbnailLengthM = root.value("lineThumbnailLengthM", tuning.lineThumbnailLengthM);
            tuning.viewerTrackLengthM = root.value("viewerTrackLengthM", tuning.viewerTrackLengthM);
            tuning.viewerCameraFocusScale = root.value("viewerCameraFocusScale", tuning.viewerCameraFocusScale);
            tuning.thumbnailFovYDeg = root.value("thumbnailFovYDeg", tuning.thumbnailFovYDeg);
            tuning.thumbnailBoxFitMargin = root.value("thumbnailBoxFitMargin", tuning.thumbnailBoxFitMargin);
            tuning.thumbnailMinBoundingRadiusM = root.value("thumbnailMinBoundingRadiusM", tuning.thumbnailMinBoundingRadiusM);
            tuning.thumbnailFarClipRadiusMultiplier = root.value("thumbnailFarClipRadiusMultiplier", tuning.thumbnailFarClipRadiusMultiplier);
            tuning.thumbnailEndFaceSectionScale = root.value("thumbnailEndFaceSectionScale", tuning.thumbnailEndFaceSectionScale);
            tuning.thumbnailSampleSpacingM = root.value("thumbnailSampleSpacingM", tuning.thumbnailSampleSpacingM);
            return tuning;
        });
    return kTuning;
}

} // namespace rs::app::plugin_browser
