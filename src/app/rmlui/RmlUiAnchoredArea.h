//
// RmlUiAnchoredArea.h - 点に追従する要素を動かせる範囲を文書から測る
//
// 左右のタブは、開いていれば本体とノッチ、閉じていればノッチの幅、無なら0だけ画面の端から除く。
// 上のバナーと下のバー(と、バーの凸)は、表示している間だけその外側へ寄せる。
// タブは動かしている最中(transform)の位置ではなく、開閉の状態(クラス)で決める。
//
#pragma once

namespace Rml {
class ElementDocument;
} // namespace Rml

namespace rs::app::rmlui {

// 画面の座標[px]。
struct AnchoredArea {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

// documentがnullなら画面全体を返す。screenWidth/Heightはビューポートの大きさ[px]。
AnchoredArea MeasureAnchoredArea(Rml::ElementDocument* document, double screenWidth, double screenHeight);

} // namespace rs::app::rmlui
