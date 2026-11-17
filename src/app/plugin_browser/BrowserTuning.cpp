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
            tuning.trainPhotoShortLengthM = root.value("trainPhotoShortLengthM", tuning.trainPhotoShortLengthM);
            tuning.trainPhotoLongLengthM = root.value("trainPhotoLongLengthM", tuning.trainPhotoLongLengthM);
            tuning.trainPhotoLongAzimuthDeg = root.value("trainPhotoLongAzimuthDeg", tuning.trainPhotoLongAzimuthDeg);
            tuning.trainPhotoFrontHeightRatio = root.value("trainPhotoFrontHeightRatio", tuning.trainPhotoFrontHeightRatio);
            tuning.trainPhotoSideMargin = root.value("trainPhotoSideMargin", tuning.trainPhotoSideMargin);
            tuning.trainPhotoFrontCenterY = root.value("trainPhotoFrontCenterY", tuning.trainPhotoFrontCenterY);
            tuning.trainPhotoMaxDistanceM = root.value("trainPhotoMaxDistanceM", tuning.trainPhotoMaxDistanceM);
            return tuning;
        });
    return kTuning;
}

} // namespace rs::app::plugin_browser
