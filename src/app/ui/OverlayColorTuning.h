#pragma once

#include <cstdint>

#include "../../core/DebugDraw.h"

namespace rs::app {

// ビューポートの注記(線・点・マーカー・ハイライト)の色。assets/config/overlay_colors.json から読む。
// JSONの値は"#rrggbbaa"。読めない・欠けたキーは、ここの初期値を使う。
// 同じ意味の色は1つのキーにまとめる(ホバー・選択・ドラッグ・不成立・成立は、画面をまたいで共通)。
struct OverlayColorTuning {
    // 操作の状態。
    uint32_t hover = rs::core::PackRgba(255, 255, 0, 255);
    uint32_t select = rs::core::PackRgba(80, 220, 255, 255);
    uint32_t drag = rs::core::PackRgba(255, 120, 255, 255);
    uint32_t invalid = rs::core::PackRgba(255, 80, 80, 255);
    uint32_t valid = rs::core::PackRgba(80, 255, 120, 255);
    // 配置物のハイライトの塗りの不透明度(色は上の hover / select / drag)。
    float tintAlpha = 0.35f;
    uint32_t pointIdle = rs::core::PackRgba(200, 200, 200, 255);
    // 不成立のプレビュー線(濃い・薄い)。
    uint32_t invalidLine = rs::core::PackRgba(255, 80, 80, 200);
    uint32_t invalidLineFaint = rs::core::PackRgba(255, 80, 80, 160);

    // 敷設のプレビュー(区間の種類ごと)。
    uint32_t layingLine = rs::core::PackRgba(90, 170, 255, 230);
    uint32_t layingTransition = rs::core::PackRgba(255, 175, 55, 240);
    uint32_t layingTransitionFill = rs::core::PackRgba(255, 175, 55, 100);
    uint32_t layingOtherFill = rs::core::PackRgba(140, 220, 140, 100);
    uint32_t layingEndNode = rs::core::PackRgba(255, 200, 80, 255);
    uint32_t layingAnchor = rs::core::PackRgba(60, 120, 255, 255);
    // edgeに近づいたときのマーカー(分岐の始点・Alt+クリックの分割位置)。できない位置は invalid を使う。
    uint32_t branchMarker = rs::core::PackRgba(255, 150, 40, 255);
    uint32_t splitMarker = rs::core::PackRgba(80, 210, 255, 255);
    // 敷設ツールで点が置かれる位置の円のうち、並走位置のもの(ノードは valid、スナップなしは edgeMarkerIdle)。
    uint32_t parallelSnapCursor = rs::core::PackRgba(90, 200, 255, 255);

    // エッジの注記。
    uint32_t edgeIdle = rs::core::PackRgba(140, 220, 255, 160);
    uint32_t edgeSelected = rs::core::PackRgba(255, 220, 40, 255);
    uint32_t edgeHovered = rs::core::PackRgba(255, 255, 255, 255);
    uint32_t edgeMarkerIdle = rs::core::PackRgba(180, 180, 180, 150);
    uint32_t edgeRoleStart = rs::core::PackRgba(90, 190, 255, 220);
    uint32_t edgeRoleEnd = rs::core::PackRgba(110, 255, 140, 220);

    // 区間編集・信号閉塞・文字・プラグイン設定の対象。
    uint32_t sectionLine = rs::core::PackRgba(255, 175, 55, 220);
    uint32_t sectionRing = rs::core::PackRgba(0, 0, 0, 200);
    uint32_t signalExtent = rs::core::PackRgba(210, 70, 255, 255);
    uint32_t labelText = rs::core::PackRgba(255, 255, 255, 255);
    uint32_t labelStem = rs::core::PackRgba(255, 255, 255, 170);
    // 不透明度は使う側が動かすので、色だけを持つ。
    uint32_t pluginTargetBase = rs::core::PackRgba(255, 0, 0, 255);

    // 閉塞を色分けする彩度。
    float blockSaturation = 0.72f;
    // 閉塞を色分けする明度。
    float blockValue = 0.92f;

    static const OverlayColorTuning& Embedded();
};

// 色の不透明度(0〜255)だけを差し替える。
constexpr uint32_t WithAlpha(uint32_t packedColor, uint32_t alpha) {
    return (packedColor & 0x00FFFFFFu) | (alpha << 24u);
}

// ハイライト用の色(RGBA、0〜1)。不透明度はtintAlpha。
struct OverlayTint {
    float rgba[4];
};
OverlayTint MakeOverlayTint(uint32_t packedColor, float alpha);

} // namespace rs::app
