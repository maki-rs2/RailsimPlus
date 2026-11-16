#include "RmlUiDropDown.h"

#include <algorithm>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiElementQuery.h"

namespace rs::app::rmlui {

namespace {

// widgets.rcss側のキーフレーム名(selectbox-reveal-down/up)と対にするクラス名。
constexpr const char* kRevealDownClass = "reveal-down";
constexpr const char* kRevealUpClass = "reveal-up";
// 開いた最初のフレームに付ける目印。位置が決まる次のフレームまで向きの判定を待つために使う。
constexpr const char* kRevealPendingAttribute = "data-reveal-pending";

// select の非DOM子(RmlUiが内部で足す `selectbox`)を探す。通常の子ではないため
// 非DOM子まで含めて走査する。
Rml::Element* FindSelectBox(Rml::Element& select) {
    return FindChildIf(select, [](Rml::Element& child) { return child.GetTagName() == "selectbox"; },
                       /*includeNonDom=*/true);
}

// 画面の右端に残す余白(dp)。
constexpr float kScreenMargin = 8.0f;

// 選択肢のいちばん長い文字に合わせて、箱の幅を広げる。箱の幅は既定で select の幅になり、長い選択肢は
// 折り返されて箱からはみ出すため。画面の右端を超えるときは画面に収まる幅までにして、足りない分は折り返す
// (高さは選択肢の中身に合わせて伸びる)。select より狭くはしない。
void FitSelectBoxWidth(Rml::Element& select, Rml::Element& selectBox) {
    float widest = 0.0f;
    const int count = selectBox.GetNumChildren();
    for (int i = 0; i < count; ++i) {
        Rml::Element* const option = selectBox.GetChild(i);
        const float text = static_cast<float>(Rml::ElementUtilities::GetStringWidth(option, option->GetInnerRML()));
        const float chrome = option->GetBox().GetSizeAcross(Rml::BoxDirection::Horizontal, Rml::BoxArea::Border,
                                                            Rml::BoxArea::Padding);
        widest = std::max(widest, text + chrome);
    }
    const float boxChrome = selectBox.GetBox().GetSizeAcross(Rml::BoxDirection::Horizontal, Rml::BoxArea::Border,
                                                             Rml::BoxArea::Padding);
    float width = std::max(widest + boxChrome, select.GetBox().GetSize(Rml::BoxArea::Border).x);
    if (const Rml::Context* const context = select.GetContext()) {
        const float room = static_cast<float>(context->GetDimensions().x) -
                           select.GetAbsoluteOffset(Rml::BoxArea::Border).x - kScreenMargin;
        width = std::min(width, std::max(room, select.GetBox().GetSize(Rml::BoxArea::Border).x));
    }
    selectBox.SetProperty("width", Rml::ToString(width) + "px");
}

} // namespace

void SyncDropDownReveal(Rml::ElementDocument& document) {
    Rml::ElementList selects;
    document.GetElementsByTagName(selects, "select");

    bool openedThisFrame = false;
    for (Rml::Element* select : selects) {
        Rml::Element* const selectBox = FindSelectBox(*select);
        if (selectBox == nullptr) continue;

        if (!selectBox->IsPseudoClassSet("checked")) {
            selectBox->SetClass(kRevealDownClass, false);
            selectBox->SetClass(kRevealUpClass, false);
            selectBox->RemoveAttribute(kRevealPendingAttribute);
            continue;
        }
        if (selectBox->IsClassSet(kRevealDownClass) || selectBox->IsClassSet(kRevealUpClass)) continue;

        // 開いた直後の1フレームはまだ位置が決まっていない
        // (WidgetDropDown::OnRenderがこのフレームの描画中に確定させる)ため、
        // 目印だけ付けて、次のフレームでここに来たときに初めて向きを判定する。
        if (!selectBox->HasAttribute(kRevealPendingAttribute)) {
            selectBox->SetAttribute(kRevealPendingAttribute, true);
            FitSelectBoxWidth(*select, *selectBox);
            continue;
        }
        selectBox->RemoveAttribute(kRevealPendingAttribute);
        const bool opensUpward = selectBox->GetOffsetTop() < 0.f;
        selectBox->SetClass(opensUpward ? kRevealUpClass : kRevealDownClass, true);
        openedThisFrame = true;
    }

    // クラスを付けた直後にレイアウトを更新しておくと、開いた向きが決まった
    // このフレームの描画からアニメーションが始まる(1フレーム待たされない)。
    if (openedThisFrame) document.UpdateDocument();
}

} // namespace rs::app::rmlui
