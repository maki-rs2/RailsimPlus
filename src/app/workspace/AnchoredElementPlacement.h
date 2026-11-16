//
// AnchoredElementPlacement.h - 点に追従する要素を画面上へ置く計算
//
// 入力は点をviewProjで変換したクリップ座標(x, y, w)と、画面・動かせる範囲・要素の大きさ。
// 動かせる範囲は画面から、左右のタブ・上のバナー・下のバーを除いた矩形。
// ProjectToScreenはカメラの後ろ(w<=0)を「見えない」とするだけでクランプには使えないので、
// クリップ座標から直接求める。RmlUiに依存しない。
//
#pragma once

#include <algorithm>
#include <cmath>

namespace rs::app::workspace {

// クリップ座標(行ベクトル慣習のviewProj適用後)。wが負ならカメラの後ろ。
struct ClipPoint {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
};

struct AnchoredElementPlacementInput {
    ClipPoint anchorClip;
    double screenWidth = 0.0;  // 画面の大きさ[px]
    double screenHeight = 0.0;
    // カードを動かせる範囲(画面の座標[px])。画面の内側で、固定のUIに重ならない矩形。
    double areaLeft = 0.0;
    double areaTop = 0.0;
    double areaRight = 0.0;
    double areaBottom = 0.0;
    double elementWidth = 0.0;    // カードの大きさ[px]
    double elementHeight = 0.0;
    double margin = 0.0;       // 動かせる範囲の端からカードまでの余白[px]
    double anchorGap = 0.0;    // 点とカードの下端のすき間[px]
};

struct AnchoredElementPlacement {
    double left = 0.0; // カードの左上[px]
    double top = 0.0;
    bool clamped = false; // 動かせる範囲の端へ寄せたか(点がカードの本来の位置にないか)
};

// wがこの値以下の点は、画面への投影が定まらないのでカメラの後ろとして扱う。
inline constexpr double kClipFrontMinW = 1e-6;

// カードの中心を動かせる範囲[lo, hi]。areaLo〜areaHiは動かせる範囲。カードが範囲より大きいときは範囲の中央1点にする。
inline void ElementCenterRange(double areaLo, double areaHi, double elementSize, double margin, double& lo, double& hi) {
    lo = areaLo + margin + elementSize * 0.5;
    hi = areaHi - margin - elementSize * 0.5;
    if (lo > hi) lo = hi = (areaLo + areaHi) * 0.5;
}

// カードの置き場所を求める。
// 画面の前にある点は、点の真上にカードの下端を置き、はみ出した分を動かせる範囲へ寄せる。
// カメラの後ろにある点は、点の方向(クリップのx,y。向きは反転しない)へ、動かせる範囲の中心から
// カードを動かせる範囲の端まで延ばした位置に置く。真後ろで方向が定まらないときは範囲の下端。
inline AnchoredElementPlacement PlaceAnchoredElement(const AnchoredElementPlacementInput& input) {
    double loX = 0.0, hiX = 0.0, loY = 0.0, hiY = 0.0;
    ElementCenterRange(input.areaLeft, input.areaRight, input.elementWidth, input.margin, loX, hiX);
    ElementCenterRange(input.areaTop, input.areaBottom, input.elementHeight, input.margin, loY, hiY);

    double centerX = 0.0;
    double centerY = 0.0;
    bool clamped = false;
    if (input.anchorClip.w > kClipFrontMinW) {
        const double ndcX = input.anchorClip.x / input.anchorClip.w;
        const double ndcY = input.anchorClip.y / input.anchorClip.w;
        const double anchorX = (ndcX * 0.5 + 0.5) * input.screenWidth;
        const double anchorY = (1.0 - (ndcY * 0.5 + 0.5)) * input.screenHeight;
        centerX = anchorX;
        centerY = anchorY - input.anchorGap - input.elementHeight * 0.5;
        const double clampedX = std::clamp(centerX, loX, hiX);
        const double clampedY = std::clamp(centerY, loY, hiY);
        clamped = clampedX != centerX || clampedY != centerY;
        centerX = clampedX;
        centerY = clampedY;
    } else {
        // 画面の座標系(yは下向き)の方向。クリップのyは上向きなので符号を反転する。
        double directionX = input.anchorClip.x;
        double directionY = -input.anchorClip.y;
        if (directionX == 0.0 && directionY == 0.0) directionY = 1.0;
        const double midX = (loX + hiX) * 0.5;
        const double midY = (loY + hiY) * 0.5;
        const double halfX = (hiX - loX) * 0.5;
        const double halfY = (hiY - loY) * 0.5;
        // 中心から方向へ進んで、範囲の端に最初に当たるまでの倍率。
        double scale = 0.0;
        const double scaleX = directionX != 0.0 ? halfX / std::abs(directionX) : -1.0;
        const double scaleY = directionY != 0.0 ? halfY / std::abs(directionY) : -1.0;
        if (scaleX < 0.0) {
            scale = scaleY;
        } else if (scaleY < 0.0) {
            scale = scaleX;
        } else {
            scale = std::min(scaleX, scaleY);
        }
        centerX = midX + directionX * scale;
        centerY = midY + directionY * scale;
        clamped = true;
    }
    return {centerX - input.elementWidth * 0.5, centerY - input.elementHeight * 0.5, clamped};
}

// カメラから先頭車までの距離がmaxDistanceM以内なら出す(ちょうどの距離は出す)。
inline bool IsWithinCardDistance(float distanceM, float maxDistanceM) {
    return distanceM <= maxDistanceM;
}

} // namespace rs::app::workspace
