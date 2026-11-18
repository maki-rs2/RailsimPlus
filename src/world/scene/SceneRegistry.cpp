//
// SceneRegistry.cpp
//
#include "SceneRegistry.h"

#include <algorithm>
#include <utility>

namespace rs::world {

Guid SceneRegistry::AddEntity(SceneEntity entity) {
    entity.guid = GenerateGuid();
    const Guid guid = entity.guid;
    const bool persistent = IsUserPlacement(entity);
    entities_.push_back(std::move(entity));
    guidToIndex_[guid] = entities_.size() - 1;
    MarkStructureChanged(persistent);
    return guid;
}

Guid SceneRegistry::RestoreEntity(SceneEntity entity) {
    // .rs2取り込みのように元データがGUIDを持たない経路でもここへ来るため、
    // 未採番なら発番する(既存のGUIDは.rsl再読込での同一性のため保持する)。
    // 同じguidが既にいるときは振り直す(索引が上書きされて片方が引けなくなるのを避ける)。
    if (GuidIsNil(entity.guid) || guidToIndex_.count(entity.guid) > 0) entity.guid = GenerateGuid();
    const Guid guid = entity.guid;
    const bool persistent = IsUserPlacement(entity);
    entities_.push_back(std::move(entity));
    guidToIndex_[guid] = entities_.size() - 1;
    MarkStructureChanged(persistent);
    return guid;
}

Guid SceneRegistry::RestoreEntityAt(size_t index, SceneEntity entity) {
    if (GuidIsNil(entity.guid) || guidToIndex_.count(entity.guid) > 0) entity.guid = GenerateGuid();
    const Guid guid = entity.guid;
    const bool persistent = IsUserPlacement(entity);
    const size_t position = std::min(index, entities_.size());
    entities_.insert(entities_.begin() + static_cast<std::ptrdiff_t>(position), std::move(entity));
    RebuildIndex();
    MarkStructureChanged(persistent);
    return guid;
}

void SceneRegistry::RemoveEntity(const Guid& guid) {
    const std::unordered_map<Guid, size_t, GuidHash>::iterator found = guidToIndex_.find(guid);
    if (found == guidToIndex_.end()) return;
    const bool persistent = IsUserPlacement(entities_[found->second]);
    entities_.erase(entities_.begin() + static_cast<std::ptrdiff_t>(found->second));
    // eraseで後続要素のインデックスが全てずれるため、マップを丸ごと再構築する
    // (削除は稀な操作なのでO(N)再構築でよい)。
    RebuildIndex();
    MarkStructureChanged(persistent);
}

void SceneRegistry::RemoveByCategory(SceneEntityCategory category) {
    RemoveUserPlacementsIf([category](const SceneEntity& e) { return e.category == category; });
}

void SceneRegistry::UpdatePlacement(const Guid& guid, ScenePlacement placement) {
    const std::unordered_map<Guid, size_t, GuidHash>::iterator found = guidToIndex_.find(guid);
    if (found == guidToIndex_.end()) return;
    entities_[found->second].placement = std::move(placement);
    ++entities_[found->second].placementRevision;
}


void SceneRegistry::CommitPlacement(const Guid& guid, ScenePlacement placement) {
    const std::unordered_map<Guid, size_t, GuidHash>::iterator found = guidToIndex_.find(guid);
    if (found == guidToIndex_.end()) return;
    SceneEntity& entity = entities_[found->second];
    const FreePlacement* currentFree = std::get_if<FreePlacement>(&entity.placement);
    const FreePlacement* nextFree = std::get_if<FreePlacement>(&placement);
    if (currentFree != nullptr && nextFree != nullptr &&
        currentFree->position.x == nextFree->position.x && currentFree->position.y == nextFree->position.y &&
        currentFree->position.z == nextFree->position.z && currentFree->yawDeg == nextFree->yawDeg &&
        currentFree->pitchDeg == nextFree->pitchDeg && currentFree->rollDeg == nextFree->rollDeg) return;
    bool alreadyPending = false;
    for (const PlacementChange& change : pendingPlacementChanges_) {
        if (change.entityId == guid) { alreadyPending = true; break; }
    }
    if (!alreadyPending) pendingPlacementChanges_.push_back({guid, entity.placement});
    entity.placement = std::move(placement);
    ++entity.placementRevision;
    ++revision_;
    if (IsUserPlacement(entity)) ++persistentRevision_;
}


void SceneRegistry::UpdatePlacementWithScene(const Guid& guid, ScenePlacement placement,
                                             size_t placementSceneIndex) {
    const std::unordered_map<Guid, size_t, GuidHash>::iterator found = guidToIndex_.find(guid);
    if (found == guidToIndex_.end()) return;
    entities_[found->second].placement = std::move(placement);
    entities_[found->second].placementSceneIndex = placementSceneIndex;
    ++entities_[found->second].placementRevision;
}

bool SceneRegistry::WriteSwitchOption(SceneEntity& entity, size_t index, int32_t value) {
    const int32_t current = index < entity.switchOptions.size() ? entity.switchOptions[index] : 0;
    if (current == value) return false;
    // 同じ個体が続けて変わっても、積み直しに要るのは最初の変更前の値だけ。
    bool alreadyPending = false;
    for (const SwitchChange& change : pendingSwitchChanges_) {
        if (change.entityId == entity.guid) { alreadyPending = true; break; }
    }
    if (!alreadyPending) pendingSwitchChanges_.push_back({entity.guid, entity.switchOptions});
    if (index >= entity.switchOptions.size()) {
        entity.switchOptions.resize(index + 1, 0);
    }
    entity.switchOptions[index] = value;
    ++revision_;
    return true;
}

void SceneRegistry::SetSwitchOption(const Guid& guid, size_t index, int32_t value) {
    const std::unordered_map<Guid, size_t, GuidHash>::iterator found = guidToIndex_.find(guid);
    if (found == guidToIndex_.end()) return;
    SceneEntity& entity = entities_[found->second];
    const auto saved = savedSwitchOptions_.find(guid);
    if (saved != savedSwitchOptions_.end()) {
        // 一時的な値の表示中に保存値を変えたら、控えの方を書き換える(表示は戻すときに反映される)。
        std::vector<int32_t>& options = saved->second;
        const int32_t current = index < options.size() ? options[index] : 0;
        if (current == value) return;
        if (index >= options.size()) options.resize(index + 1, 0);
        options[index] = value;
        if (IsUserPlacement(entity)) ++persistentRevision_;
        return;
    }
    if (WriteSwitchOption(entity, index, value) && IsUserPlacement(entity)) ++persistentRevision_;
}

void SceneRegistry::SetRuntimeSwitchOption(const Guid& guid, size_t index, int32_t value) {
    const std::unordered_map<Guid, size_t, GuidHash>::iterator found = guidToIndex_.find(guid);
    if (found == guidToIndex_.end()) return;
    SceneEntity& entity = entities_[found->second];
    savedSwitchOptions_.try_emplace(guid, entity.switchOptions);
    WriteSwitchOption(entity, index, value);
}

void SceneRegistry::ClearRuntimeSwitchOptions(const Guid& guid) {
    const auto saved = savedSwitchOptions_.find(guid);
    if (saved == savedSwitchOptions_.end()) return;
    std::vector<int32_t> options = std::move(saved->second);
    savedSwitchOptions_.erase(saved);
    const std::unordered_map<Guid, size_t, GuidHash>::iterator found = guidToIndex_.find(guid);
    if (found == guidToIndex_.end()) return;
    SceneEntity& entity = entities_[found->second];
    const size_t count = std::max(options.size(), entity.switchOptions.size());
    for (size_t index = 0; index < count; ++index) {
        WriteSwitchOption(entity, index, index < options.size() ? options[index] : 0);
    }
}

const std::vector<int32_t>& SceneRegistry::SavedSwitchOptions(const SceneEntity& entity) const {
    const auto saved = savedSwitchOptions_.find(entity.guid);
    return saved == savedSwitchOptions_.end() ? entity.switchOptions : saved->second;
}

void SceneRegistry::ReplaceEntityPlugin(const Guid& guid, std::string folderUtf8,
                                        std::vector<int32_t> switchOptions) {
    const std::unordered_map<Guid, size_t, GuidHash>::iterator found = guidToIndex_.find(guid);
    if (found == guidToIndex_.end()) return;
    SceneEntity& entity = entities_[found->second];
    entity.folderUtf8 = std::move(folderUtf8);
    entity.switchOptions = std::move(switchOptions);
    savedSwitchOptions_.erase(guid);
    MarkStructureChanged(IsUserPlacement(entity));
}

const SceneEntity* SceneRegistry::FindEntity(const Guid& guid) const {
    const std::unordered_map<Guid, size_t, GuidHash>::const_iterator found = guidToIndex_.find(guid);
    return found == guidToIndex_.end() ? nullptr : &entities_[found->second];
}

std::vector<SceneRegistry::SwitchChange> SceneRegistry::TakePendingSwitchChanges() {
    std::vector<SwitchChange> taken;
    taken.swap(pendingSwitchChanges_);
    return taken;
}

std::vector<SceneRegistry::PlacementChange> SceneRegistry::TakePendingPlacementChanges() {
    std::vector<PlacementChange> taken;
    taken.swap(pendingPlacementChanges_);
    return taken;
}

void SceneRegistry::MarkStructureChanged(bool persistent) {
    ++revision_;
    ++structureRevision_;
    if (persistent) ++persistentRevision_;
}

void SceneRegistry::RemoveUserPlacementsIf(const std::function<bool(const SceneEntity&)>& matches) {
    const size_t before = entities_.size();
    entities_.erase(std::remove_if(entities_.begin(), entities_.end(),
                                    [&matches](const SceneEntity& e) {
                                        return IsUserPlacement(e) && matches(e);
                                    }),
                    entities_.end());
    if (entities_.size() != before) {
        RebuildIndex();
        MarkStructureChanged(true);
    }
}

void SceneRegistry::RebuildIndex() {
    guidToIndex_.clear();
    guidToIndex_.reserve(entities_.size());
    for (size_t i = 0; i < entities_.size(); ++i) {
        guidToIndex_[entities_[i].guid] = i;
    }
    // 消えた個体の保存値の控えは捨てる。
    std::erase_if(savedSwitchOptions_, [this](const auto& entry) { return !guidToIndex_.contains(entry.first); });
}

} // namespace rs::world
