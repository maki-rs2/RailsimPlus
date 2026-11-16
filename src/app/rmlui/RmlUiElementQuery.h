//
// RmlUiElementQuery.h - RmlUiの要素を探す小さな共通関数
//
#pragma once

#include <RmlUi/Core/Element.h>

namespace rs::app::rmlui {

// parentの子のうち、predicate(Rml::Element&)がtrueを返す最初の要素を返す。無ければnullptr。
// includeNonDomがtrueのときは、RmlUiが内部で足す非DOMの子(select の selectbox など)も探す。
template <typename Predicate>
Rml::Element* FindChildIf(Rml::Element& parent, Predicate predicate, bool includeNonDom = false) {
    const int childCount = parent.GetNumChildren(includeNonDom);
    for (int i = 0; i < childCount; ++i) {
        Rml::Element* const child = parent.GetChild(i);
        if (predicate(*child)) return child;
    }
    return nullptr;
}

} // namespace rs::app::rmlui
