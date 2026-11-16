//
// RmlUiSegmented.h - セグメントボタン(`.segmented` 内の `.segment`)のハイライト(`.segment-indicator`)の位置合わせ。
//
// 少数の決まった選択肢から1つを選ぶ部品。
// 見た目は assets/ui/theme/widgets.rcss の `.segment` / `.segment-indicator` が受け持つ。
// 選択の付け替えはデータバインディング(data-class-selected)が行う。ここでは、
// ハイライトの left/width を選択中の区画(`selected` クラスの区画)に合わせてスライドさせる
// (SyncSegmentIndicators)。
//
#pragma once

namespace Rml {
class ElementDocument;
} // namespace Rml

namespace rs::app::rmlui {

// 文書内のすべての `.segmented` について、ハイライトの left/width を選択中の区画に合わせる。
// 区画の位置はレイアウト後に決まるため、Context::Update() の後に毎フレーム呼ぶ。
// 目標が変わったときだけ動かし、初回は瞬時に置き、2回目以降は Element::Animate でスライドさせる
// (RmlUi の transition はクラス等による定義の切り替えでしか働かず、インラインで変えた値は補間されないため)。
void SyncSegmentIndicators(Rml::ElementDocument& document);

} // namespace rs::app::rmlui
