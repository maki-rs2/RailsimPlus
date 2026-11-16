#include "TrainTagRegistry.h"

#include <algorithm>
#include <utility>

namespace rs::world {

int TrainTagRegistry::AddTag(std::string label) {
    const int tagId = nextTagId_;
    ++nextTagId_;
    tags_.push_back({tagId, std::move(label)});
    return tagId;
}

void TrainTagRegistry::RestoreTag(TrainTagDef tag) {
    nextTagId_ = std::max(nextTagId_, tag.id + 1);
    tags_.push_back(std::move(tag));
}

void TrainTagRegistry::ClearTags() {
    tags_.clear();
    formationTagIds_.clear();
    trainFormation_.clear();
}

void TrainTagRegistry::RemoveTag(int tagId) {
    const std::vector<TrainTagDef>::iterator iterator = std::remove_if(
        tags_.begin(), tags_.end(), [tagId](const TrainTagDef& tag) { return tag.id == tagId; });
    tags_.erase(iterator, tags_.end());
    for (std::pair<const Guid, std::vector<int>>& entry : formationTagIds_) {
        std::vector<int>& ids = entry.second;
        ids.erase(std::remove(ids.begin(), ids.end(), tagId), ids.end());
    }
}

const std::vector<TrainTagDef>& TrainTagRegistry::Tags() const {
    return tags_;
}

void TrainTagRegistry::SetFormationTags(const Guid& formationId, std::vector<int> tagIds) {
    formationTagIds_[formationId] = std::move(tagIds);
}

const std::vector<int>& TrainTagRegistry::FormationTags(const Guid& formationId) const {
    const std::unordered_map<Guid, std::vector<int>, GuidHash>::const_iterator iterator =
        formationTagIds_.find(formationId);
    if (iterator != formationTagIds_.end()) return iterator->second;
    static const std::vector<int> kEmptyTags;
    return kEmptyTags;
}

const std::unordered_map<Guid, std::vector<int>, GuidHash>& TrainTagRegistry::FormationTagAssignments() const {
    return formationTagIds_;
}

void TrainTagRegistry::CopyFormationTags(const Guid& sourceFormationId, const Guid& destinationFormationId) {
    // FormationTagsの戻り値は同じ表の要素への参照なので、代入の前に値で受ける。
    std::vector<int> tagIds = FormationTags(sourceFormationId);
    formationTagIds_[destinationFormationId] = std::move(tagIds);
}

void TrainTagRegistry::LinkTrainToFormation(TrainId trainId, const Guid& formationId) {
    trainFormation_[trainId] = formationId;
}

Guid TrainTagRegistry::FormationOfTrain(TrainId trainId) const {
    const std::unordered_map<TrainId, Guid, GuidHash>::const_iterator iterator = trainFormation_.find(trainId);
    return iterator != trainFormation_.end() ? iterator->second : NilGuid();
}

std::vector<TrainId> TrainTagRegistry::TrainsOfFormation(const Guid& formationId) const {
    std::vector<TrainId> trains;
    for (const std::pair<const TrainId, Guid>& link : trainFormation_) {
        if (link.second == formationId) trains.push_back(link.first);
    }
    return trains;
}

bool TrainTagRegistry::HasTrainOfFormation(const Guid& formationId) const {
    return std::any_of(trainFormation_.begin(), trainFormation_.end(),
                       [&formationId](const std::pair<const TrainId, Guid>& link) { return link.second == formationId; });
}

const std::unordered_map<TrainId, Guid, GuidHash>& TrainTagRegistry::TrainFormationLinks() const {
    return trainFormation_;
}

const std::vector<int>& TrainTagRegistry::TrainTags(TrainId trainId) const {
    const std::unordered_map<TrainId, Guid, GuidHash>::const_iterator iterator = trainFormation_.find(trainId);
    if (iterator == trainFormation_.end()) {
        static const std::vector<int> kEmptyTags;
        return kEmptyTags;
    }
    return FormationTags(iterator->second);
}

void TrainTagRegistry::RemoveFormation(const Guid& formationId) {
    formationTagIds_.erase(formationId);
    for (std::unordered_map<TrainId, Guid, GuidHash>::iterator iterator = trainFormation_.begin();
         iterator != trainFormation_.end();) {
        if (iterator->second == formationId) iterator = trainFormation_.erase(iterator);
        else ++iterator;
    }
}

void TrainTagRegistry::RemoveTrain(TrainId trainId) {
    trainFormation_.erase(trainId);
}

}  // namespace rs::world
