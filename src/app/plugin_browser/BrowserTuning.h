#pragma once

namespace rs::app::plugin_browser {

struct BrowserTuning {
    // 断面や架線の形を読み取れるサムネイルの直線長[m]。
    float railThumbnailLengthM = 9.0f;
    float girderThumbnailLengthM = 40.0f;
    float lineThumbnailLengthM = 8.0f;
    // 単体表示で敷設時の見え方を示す長さ[m]と、最初のカメラが収める範囲の倍率。
    float viewerTrackLengthM = 200.0f;
    float viewerCameraFocusScale = 1.5f;

    // サムネイルの遠近の歪みを抑える縦視野角[deg]。
    float thumbnailFovYDeg = 15.0f;
    // サムネイルの外接箱に対する余白倍率。
    float thumbnailBoxFitMargin = 1.04f;
    // 点だけのモデルでも距離を保つ外接球半径の下限[m]。
    float thumbnailMinBoundingRadiusM = 0.01f;
    // 遠クリップ面に加える外接球半径の倍率。
    float thumbnailFarClipRadiusMultiplier = 2.0f;
    // 先頭アイコンの断面に掛ける倍率。
    float thumbnailEndFaceSectionScale = 1.0f;
    // サムネイルの軌道サンプル間隔[m]。
    float thumbnailSampleSpacingM = 1.0f;

    static const BrowserTuning& Embedded();
};

} // namespace rs::app::plugin_browser
