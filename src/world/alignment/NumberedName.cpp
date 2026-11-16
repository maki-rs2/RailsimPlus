#include "NumberedName.h"

namespace rs::world {

std::string NextNumberedName(std::string_view prefix,
                             const std::unordered_set<std::string>& usedNames) {
    for (size_t number = 1;; ++number) {
        std::string name(prefix);
        name += std::to_string(number);
        if (!usedNames.contains(name)) return name;
    }
}

}  // namespace rs::world
