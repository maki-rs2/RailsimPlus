//
// RmlUiWindowClamp.h - ドラッグで動かせるウィンドウを、画面の中へ収める。
//
// ウィンドウ(タイトルバーを <handle move_target="#parent"> にした要素)は、ドラッグ中は RmlUi が
// 画面の端で止める(edge_margin)。ただし RmlUi が範囲を確かめるのはドラッグの開始時だけなので、
// 動かしたあとでアプリのウィンドウを縮めると、画面の外に取り残される。ここで毎フレーム確かめ、
// はみ出した分だけ画面の中へ戻す。
//
#pragma once

namespace Rml {
class ElementDocument;
} // namespace Rml

namespace rs::app::rmlui {

// 文書内のすべての <handle> の親(ドラッグで動かすウィンドウ)について、画面からはみ出していれば
// 画面の中へ戻す。はみ出していなければ何もしない。ウィンドウが画面より大きい向きは、左端・上端を
// 画面の左端・上端にそろえる。要素の位置はレイアウト後に決まるため、Context::Update() の後に毎フレーム呼ぶ。
void ClampMovableWindowsIntoView(Rml::ElementDocument& document);

} // namespace rs::app::rmlui
