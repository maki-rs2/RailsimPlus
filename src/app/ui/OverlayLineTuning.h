#pragma once

namespace rs::app {

// ビューポートの注記の線の太さと点の大きさ。assets/config/overlay_line_tuning.json から読む。
// 細いと見づらいという実機の声から、線は最低でもminWidthPx以上にそろえる。
struct OverlayLineTuning {
    // 注記の線の最低の太さ[px]。
    float minWidthPx = 3.0f;
    // 選択中のエッジ・中心線の太さ[px]。minWidthPx以上にして、未選択との差を残す。
    float selectedWidthPx = 4.0f;
    // ノードの線(リング・ガイド線・ハンドルの先端の輪)の太さ[px]。
    float nodeLineWidthPx = 2.0f;
    // ドラッグ中のガイド線・接線ハンドルの軸の太さ[px]。
    float guideLineWidthPx = 1.5f;
    // 区間編集で強調する範囲の線の太さ[px]。
    float sectionLineWidthPx = 5.0f;
    // ノードの点の半径[px](通常・ホバー)。
    float nodeRadiusPx = 4.0f;
    float nodeHoverRadiusPx = 6.0f;
    // 選択中ノード・区間の点を囲むリングの半径[px]。
    float selectRingRadiusPx = 8.0f;
    // 情景のマーカー・ワープ端のノードを囲む図形の半径(四角は半辺)[px]。
    float markerRadiusPx = 9.0f;
    // ノードのドラッグ先を示す輪の半径[px]。
    float dragTipRadiusPx = 10.0f;
    // 塗りつぶしの小さい点(接線ハンドルの先端・区間の点・敷設の始点)の半径[px](通常・ホバー)。
    float dotRadiusPx = 5.0f;
    float dotHoverRadiusPx = 7.0f;
    // 敷設ツールで点が置かれる位置を示す小さい円の半径[px]。
    float snapCursorRadiusPx = 4.0f;
    // 高さ線の足元の点の半径[px]。
    float heightFootRadiusPx = 3.0f;

    // 信号の向きを示す矢じりの間隔[m]。
    double signalChevronSpacingM = 8.0;
    // 重なる信号範囲の下線に加える太さ[px]。
    float signalRangeUnderlayExtraPx = 3.0f;
    // 信号の横棒の半幅[m]。
    float signalMarkerHalfWidthM = 1.2f;
    // 信号の矢印の長さ[m]。
    float signalArrowLengthM = 2.5f;
    // 信号の矢じりの大きさ[m]。
    float signalArrowHeadM = 0.7f;
    // 架線の点を選ぶ半径[px]。
    float wirePointPickRadiusPx = 12.0f;
    // 架線のスパンを選ぶ半径[px]。
    float wireSpanPickRadiusPx = 10.0f;
    // ドラッグ・矩形選択を開始する移動量[px]。
    float dragStartDistancePx = 4.0f;
    // 列車の配置範囲の線幅[px]。
    float trainPlacementLineWidthPx = 6.0f;
    // 列車配置の先頭の横棒の半幅[m]。
    float trainPlacementHeadBarHalfWidthM = 1.6f;
    // 列車配置の線をレールから浮かす高さ[m]。
    float trainPlacementLiftM = 0.15f;

    static const OverlayLineTuning& Embedded();
};

} // namespace rs::app
