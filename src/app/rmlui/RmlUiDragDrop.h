// RmlUiDragDrop.h - ドラッグ&ドロップのイベントで共通に使う小さな補助(ドラッグ元の要素の取得と、落とせる場所の強調)。
#pragma once

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>

namespace rs::app::rmlui {

// 落とせる場所の強調のクラス(スタイルはworkspace.rcssの.is-drop-target)。
inline constexpr const char* kDropTargetClass = "is-drop-target";

// ドラッグしている要素(dragstartを受けた要素)。無ければnullptr。
inline Rml::Element* DraggedElement(Rml::Event& event) {
    return static_cast<Rml::Element*>(event.GetParameter<void*>("drag_element", nullptr));
}

// イベントを受けた要素の強調を付ける・外す。
inline void SetDropHighlight(Rml::Event& event, bool on) {
    if (Rml::Element* const element = event.GetCurrentElement()) element->SetClass(kDropTargetClass, on);
}

} // namespace rs::app::rmlui
