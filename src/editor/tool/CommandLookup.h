#pragma once

#include <cstddef>
#include <vector>

#include "../../world/alignment/Guid.h"

namespace rs::editor {

template <class T, class M>
size_t FindIndexByMember(const std::vector<T>& values, M T::*member, const rs::world::Guid& id) {
    for (size_t index = 0; index < values.size(); ++index) {
        if (values[index].*member == id) {
            return index;
        }
    }
    return values.size();
}

}  // namespace rs::editor
