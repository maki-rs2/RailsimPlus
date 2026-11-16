//
// RmlUiDropDown.h - コンボ(`<select>`)の選択肢の箱(`selectbox`)を開くときの見た目の切り抜き。
//
// 開くときは上端(下に開く場合)または下端
// (上に開く場合)から見える範囲を広げるアニメーションにする。実体は
// assets/ui/theme/widgets.rcss の `select selectbox.reveal-down` / `.reveal-up` の
// `mask-image` キーフレームで、ここでは向きに応じたクラスを付け外しするだけ。
//
// 箱をどちら向きに開くかは RmlUi 本体(Source/Core/Elements/WidgetDropDown.cpp の OnRender)が
// レイアウト後に決めるため、`:checked` が付いた直後のフレームではまだ位置が確定していない。
// そのため Context::Update() の後に毎フレーム呼び、位置が確定した次のフレームで初めて
// 向きを判定してクラスを付ける(それまでは widgets.rcss 側で opacity: 0 にして隠す)。
//
#pragma once

namespace Rml {
class ElementDocument;
} // namespace Rml

namespace rs::app::rmlui {

// 文書内のすべての `select` について、開いている(`:checked`)箱に向きに応じた
// reveal-down/reveal-up クラスを付け、閉じた箱からは外す。
// Context::Update() の後に毎フレーム呼ぶこと。
void SyncDropDownReveal(Rml::ElementDocument& document);

} // namespace rs::app::rmlui
