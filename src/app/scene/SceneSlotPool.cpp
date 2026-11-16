//
// SceneSlotPool.cpp
//
#include "SceneSlotPool.h"

namespace rs::app {

void SceneSlotPool::InvalidatePlugins() {
    for (Slot& slot : slots_) {
        const uint64_t generation = slot.residency.generation + 1;
        slot = Slot{};
        slot.residency.generation = generation;
    }
    failedEntities_.clear();
    lastKnownCarTemplates_.clear();
    parkedByKey_.clear();
    parkedByFolder_.clear();
}

std::string SceneSlotPool::MakeParkedIndexKey(rs::world::SceneEntityCategory category,
                                               const std::string& contentKey) {
    return std::to_string(static_cast<int>(category)) + "|" + contentKey;
}

std::string SceneSlotPool::MakeParkedFolderKey(rs::world::SceneEntityCategory category,
                                                const std::string& folderUtf8,
                                                const std::string& definitionFile) {
    return std::to_string(static_cast<int>(category)) + "|" + MakePluginSourceKey(folderUtf8, definitionFile);
}

void SceneSlotPool::EraseIndex(std::vector<size_t>& indices, size_t index) {
    for (size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] != index) continue;
        indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i));
        break;
    }
}

int SceneSlotPool::FindSlotByEntityId(const rs::world::Guid& id) const {
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].used && slots_[i].entityId == id) return static_cast<int>(i);
    }
    return -1;
}

int SceneSlotPool::FindFreeSlot() const {
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].used && !slots_[i].residency.parked) return static_cast<int>(i);
    }
    return -1;
}

size_t SceneSlotPool::GrowSlot() {
    slots_.emplace_back();
    return slots_.size() - 1;
}

void SceneSlotPool::RegisterParked(size_t index) {
    const Slot& slot = slots_[index];
    parkedByKey_[MakeParkedIndexKey(slot.residency.category, slot.residency.contentKey)].push_back(index);
    parkedByFolder_[MakeParkedFolderKey(slot.residency.category, slot.residency.folderUtf8,
                                        slot.residency.definitionFile)]
        .push_back(index);
}

int SceneSlotPool::TakeParkedSlot(rs::world::SceneEntityCategory category,
                                   const std::string& contentKey) {
    const std::string key = MakeParkedIndexKey(category, contentKey);
    const std::unordered_map<std::string, std::vector<size_t>>::iterator it = parkedByKey_.find(key);
    if (it == parkedByKey_.end() || it->second.empty()) return -1;
    const size_t index = it->second.back();
    it->second.pop_back();
    if (it->second.empty()) parkedByKey_.erase(it);

    const std::string folderKey = MakeParkedFolderKey(category, slots_[index].residency.folderUtf8,
                                                      slots_[index].residency.definitionFile);
    const std::unordered_map<std::string, std::vector<size_t>>::iterator fit =
        parkedByFolder_.find(folderKey);
    if (fit != parkedByFolder_.end()) {
        EraseIndex(fit->second, index);
        if (fit->second.empty()) parkedByFolder_.erase(fit);
    }

    slots_[index].residency.parked = false;
    ++slots_[index].residency.generation;
    return static_cast<int>(index);
}

int SceneSlotPool::TakeParkedSlotByFolder(rs::world::SceneEntityCategory category,
                                          const std::string& folderUtf8,
                                          const std::string& definitionFile) {
    const std::string folderKey = MakeParkedFolderKey(category, folderUtf8, definitionFile);
    const std::unordered_map<std::string, std::vector<size_t>>::iterator fit =
        parkedByFolder_.find(folderKey);
    if (fit == parkedByFolder_.end() || fit->second.empty()) return -1;
    const size_t index = fit->second.back();
    fit->second.pop_back();
    if (fit->second.empty()) parkedByFolder_.erase(fit);

    const std::string key = MakeParkedIndexKey(category, slots_[index].residency.contentKey);
    const std::unordered_map<std::string, std::vector<size_t>>::iterator it = parkedByKey_.find(key);
    if (it != parkedByKey_.end()) {
        EraseIndex(it->second, index);
        if (it->second.empty()) parkedByKey_.erase(it);
    }

    slots_[index].residency.parked = false;
    ++slots_[index].residency.generation;
    return static_cast<int>(index);
}

int SceneSlotPool::FindOldestParkedSlot() const {
    int oldestIndex = -1;
    uint64_t oldestRevision = UINT64_MAX;
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].residency.parked) continue;
        if (slots_[i].residency.parkedAtRevision < oldestRevision) {
            oldestRevision = slots_[i].residency.parkedAtRevision;
            oldestIndex = static_cast<int>(i);
        }
    }
    return oldestIndex;
}

void SceneSlotPool::ReleaseParked(size_t index) {
    const Slot& slot = slots_[index];
    const std::string key = MakeParkedIndexKey(slot.residency.category, slot.residency.contentKey);
    const std::unordered_map<std::string, std::vector<size_t>>::iterator it = parkedByKey_.find(key);
    if (it != parkedByKey_.end()) {
        EraseIndex(it->second, index);
        if (it->second.empty()) parkedByKey_.erase(it);
    }

    const std::string folderKey = MakeParkedFolderKey(slot.residency.category, slot.residency.folderUtf8,
                                                      slot.residency.definitionFile);
    const std::unordered_map<std::string, std::vector<size_t>>::iterator fit =
        parkedByFolder_.find(folderKey);
    if (fit != parkedByFolder_.end()) {
        EraseIndex(fit->second, index);
        if (fit->second.empty()) parkedByFolder_.erase(fit);
    }
}

bool SceneSlotPool::IsEntityFailed(const rs::world::Guid& id) const {
    return failedEntities_.count(id) != 0;
}

void SceneSlotPool::MarkEntityFailed(const rs::world::Guid& id, std::string contentKey) {
    failedEntities_[id] = std::move(contentKey);
}

void SceneSlotPool::ForgetEntityFailed(const rs::world::Guid& id) {
    failedEntities_.erase(id);
}

void SceneSlotPool::RecordCarTemplate(const rs::world::Guid& entityId,
                                       const rs::legacy_plugin::TrainVehicleTemplate& carTemplate) {
    lastKnownCarTemplates_[entityId] = carTemplate;
}

const rs::legacy_plugin::TrainVehicleTemplate* SceneSlotPool::FindCachedCarTemplate(
    const rs::world::Guid& entityId) const {
    const std::unordered_map<rs::world::Guid, rs::legacy_plugin::TrainVehicleTemplate,
                             rs::world::GuidHash>::const_iterator it =
        lastKnownCarTemplates_.find(entityId);
    return it != lastKnownCarTemplates_.end() ? &it->second : nullptr;
}

} // namespace rs::app
