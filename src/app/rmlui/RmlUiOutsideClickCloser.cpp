#include "RmlUiOutsideClickCloser.h"

#include <algorithm>
#include <utility>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

namespace rs::app::rmlui {

RmlUiOutsideClickCloser::RmlUiOutsideClickCloser(Rml::ElementDocument& document,
                                                 std::vector<std::string> insideClasses,
                                                 std::function<void()> onOutside)
    : document_(document), insideClasses_(std::move(insideClasses)), onOutside_(std::move(onOutside)) {
    document_.AddEventListener("click", this, true);
}

RmlUiOutsideClickCloser::~RmlUiOutsideClickCloser() {
    document_.RemoveEventListener("click", this, true);
}

void RmlUiOutsideClickCloser::ProcessEvent(Rml::Event& event) {
    CloseIfOutside(event.GetTargetElement());
}

void RmlUiOutsideClickCloser::CloseIfOutside(Rml::Element* target) {
    for (Rml::Element* element = target; element != nullptr; element = element->GetParentNode()) {
        const bool inside = std::any_of(insideClasses_.begin(), insideClasses_.end(), [element](const std::string& name) {
            return element->IsClassSet(name);
        });
        if (inside) return;
    }
    if (onOutside_) onOutside_();
}

} // namespace rs::app::rmlui
