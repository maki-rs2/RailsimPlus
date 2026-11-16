#pragma once

namespace rs::world {

struct EditorGeometryTuning {
    float minArcRadiusMeters = 5.0f; // 円弧敷設を受理する最小半径[m]
    float maxTurnAngleRadians = 3.0543261909900763f; // 受理する最大偏角[rad](175度)
    float minAcceptableLengthMeters = 0.5f; // 敷設curveを受理する最小全長[m]
    float minTransitionRadiusMeters = 0.3f; // 緩和曲線を受理する最小半径[m]
    float tangentResidualToleranceMeters = 5e-3f; // 緩和曲線反復の接線残差許容値[m]

    // カント自動モード(TrackEdge::cantAuto)が使う逓減率[m]。最大カント(tan比)の
    // 差1あたりの遷移長で、TrackEdge::cantTransitionRatio と同じ単位。2000mなら
    // tan比0.1のカントを200mかけて逓減する。緩和曲線つきのedgeでは曲率連動の逓減長が
    // 優先され、この値は使われない。
    float autoCantTransitionRatioMeters = 2000.0f;

    // 並走候補が既存レールと重なるとみなす距離と、端より先の候補を許す長さ[m]。
    float parallelOccupiedDistanceM = 0.5f;
    float parallelMaxAlongM = 0.3f;
    // 既存形状を緩い条件で再構築する際に許す端点のずれ[m]と接線差[deg]。
    double relaxedEndPositionToleranceM = 0.05;
    double relaxedEndDirectionToleranceDeg = 0.5;

    // 取り込み点列を円弧に近似するときに許す水平距離[m]。
    float importedFilletFitToleranceM = 0.15f;
    // 取り込み円弧の平面からの弦方向のずれの許容(内積)。
    float importedCoplanarityToleranceDot = 0.03f;

    static const EditorGeometryTuning& Embedded();
};

} // namespace rs::world
