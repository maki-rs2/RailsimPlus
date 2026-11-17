#include "SwitchList.h"
#include "../../plugins/schema/PluginSchema.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace rs::app {

namespace {

// 名前が空白(半角スペース・タブ・全角スペース)だけか。全角スペースはUTF-8で E3 80 80。
bool IsBlankName(const std::string& name) {
    constexpr std::string_view kIdeographicSpace = "ã";
    std::size_t pos = 0;
    while (pos < name.size()) {
        if (name[pos] == ' ' || name[pos] == '	') {
            ++pos;
        } else if (name.compare(pos, kIdeographicSpace.size(), kIdeographicSpace) == 0) {
            pos += kIdeographicSpace.size();
        } else {
            return false;
        }
    }
    return true;
}

} // namespace

SwitchRowKind GetSwitchRowKind(const SwitchRowViewModel& row) {
    if (!row.entries.empty()) return SwitchRowKind::Choice;
    return IsBlankName(row.name) ? SwitchRowKind::Spacer : SwitchRowKind::Header;
}

std::int32_t ClampSwitchValue(const rs::legacy_pi::SwitchDef& def, std::int32_t value) {
    if (def.entries.empty()) {
        return value;
    }
    const std::int32_t maximum = static_cast<std::int32_t>(def.entries.size()) - 1;
    return std::clamp(value, 0, maximum);
}

std::vector<SwitchRowViewModel> BuildSwitchRows(
    const std::vector<rs::legacy_pi::SwitchDef>& defs,
    const std::function<std::int32_t(std::size_t index, const rs::legacy_pi::SwitchDef& def)>&
        currentValue) {
    std::vector<SwitchRowViewModel> rows;
    rows.reserve(defs.size());
    for (std::size_t index = 0; index < defs.size(); ++index) {
        const rs::legacy_pi::SwitchDef& def = defs[index];
        SwitchRowViewModel row;
        row.name = def.name;
        row.entries = def.entries;
        row.value = ClampSwitchValue(def, currentValue(index, def));
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace rs::app
