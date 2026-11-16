// OrderedGuidSelection.h - 追加順と主選択を保つGuid集合
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {

class OrderedGuidSelection {
public:
    const std::vector<rs::world::Guid>& Items() const { return items_; }
    std::size_t Count() const { return items_.size(); }
    bool Empty() const { return items_.empty(); }
    bool Contains(const rs::world::Guid& id) const {
        return std::find(items_.begin(), items_.end(), id) != items_.end();
    }
    rs::world::Guid Primary() const { return items_.empty() ? rs::world::NilGuid() : items_.back(); }
    void Clear() { items_.clear(); }
    void SetSingle(const rs::world::Guid& id) {
        items_.clear();
        if (!rs::world::GuidIsNil(id)) items_.push_back(id);
    }
    void Add(const rs::world::Guid& id) {
        if (!rs::world::GuidIsNil(id) && !Contains(id)) items_.push_back(id);
    }
    void Toggle(const rs::world::Guid& id) {
        if (rs::world::GuidIsNil(id)) return;
        const auto found = std::find(items_.begin(), items_.end(), id);
        if (found == items_.end()) items_.push_back(id);
        else items_.erase(found);
    }
    void Replace(const rs::world::Guid& removed, const rs::world::Guid& kept) {
        const auto found = std::find(items_.begin(), items_.end(), removed);
        if (found == items_.end()) return;
        if (Contains(kept)) items_.erase(found);
        else *found = kept;
    }
    void SetItems(const std::vector<rs::world::Guid>& items) { items_ = items; }

private:
    std::vector<rs::world::Guid> items_;
};

} // namespace rs::editor
