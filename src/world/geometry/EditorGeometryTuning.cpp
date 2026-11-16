#include "EditorGeometryTuning.h"

#include "../../core/ConfigJson.h"

#include <nlohmann/json.hpp>

#include "EmbeddedEditorGeometryTuning.h"

namespace rs::world {

namespace {

EditorGeometryTuning ParseEmbedded() {
    return rs::core::ReadEmbeddedConfig<EditorGeometryTuning>(
        rs::embedded::kEditorGeometryTuningJson, rs::embedded::kEditorGeometryTuningJsonSize, "EditorGeometryTuning",
        [](const nlohmann::json& root) {
            EditorGeometryTuning tuning;
            tuning.minArcRadiusMeters = root.value("minArcRadiusMeters", tuning.minArcRadiusMeters);
            tuning.maxTurnAngleRadians = root.value("maxTurnAngleRadians", tuning.maxTurnAngleRadians);
            tuning.minAcceptableLengthMeters =
                root.value("minAcceptableLengthMeters", tuning.minAcceptableLengthMeters);
            tuning.minTransitionRadiusMeters =
                root.value("minTransitionRadiusMeters", tuning.minTransitionRadiusMeters);
            tuning.tangentResidualToleranceMeters =
                root.value("tangentResidualToleranceMeters", tuning.tangentResidualToleranceMeters);
            tuning.autoCantTransitionRatioMeters =
                root.value("autoCantTransitionRatioMeters", tuning.autoCantTransitionRatioMeters);
            tuning.parallelOccupiedDistanceM = root.value("parallelOccupiedDistanceM", tuning.parallelOccupiedDistanceM);
            tuning.parallelMaxAlongM = root.value("parallelMaxAlongM", tuning.parallelMaxAlongM);
            tuning.relaxedEndPositionToleranceM = root.value("relaxedEndPositionToleranceM", tuning.relaxedEndPositionToleranceM);
            tuning.relaxedEndDirectionToleranceDeg = root.value("relaxedEndDirectionToleranceDeg", tuning.relaxedEndDirectionToleranceDeg);
            tuning.importedFilletFitToleranceM = root.value("importedFilletFitToleranceM", tuning.importedFilletFitToleranceM);
            tuning.importedCoplanarityToleranceDot = root.value("importedCoplanarityToleranceDot", tuning.importedCoplanarityToleranceDot);
            return tuning;
        });
}

} // namespace

const EditorGeometryTuning& EditorGeometryTuning::Embedded() {
    static const EditorGeometryTuning kTuning = ParseEmbedded();
    return kTuning;
}

} // namespace rs::world
