#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "../../core/Color.h"
#include "TrainId.h"

namespace rs::world {

struct TrainTagDef {
    int id = -1;
    std::string label;
    rs::core::Color color;  // 編集UIはRmlUI移行まで保留(既定値のまま)
};

// 属性タグの定義と、編成の定義(Formation)ごとのタグ、配置済み列車から配置元の編成への対応を持つ。
// タグの持ち主は編成の定義だけで、列車ごとには持たない。同じ編成から置いた列車は同じタグになる。
class TrainTagRegistry {
public:
    int AddTag(std::string label);
    void RestoreTag(TrainTagDef tag);
    // タグの定義・編成ごとのタグ・列車と編成の対応をすべて空にする。
    void ClearTags();
    // タグの定義と、全編成のタグ一覧からそのidを取り除く。
    void RemoveTag(int tagId);
    const std::vector<TrainTagDef>& Tags() const;

    void SetFormationTags(const Guid& formationId, std::vector<int> tagIds);
    const std::vector<int>& FormationTags(const Guid& formationId) const;
    const std::unordered_map<Guid, std::vector<int>, GuidHash>& FormationTagAssignments() const;
    // sourceのタグ一覧をdestinationへ写す(上書き)。列車との対応は写さない。sourceにタグが無ければdestinationのタグも空にする。
    void CopyFormationTags(const Guid& sourceFormationId, const Guid& destinationFormationId);

    // 列車の配置元の編成を登録する。登録済みなら上書きする。
    void LinkTrainToFormation(TrainId trainId, const Guid& formationId);
    // 配置元の編成。登録が無ければNilGuid。
    Guid FormationOfTrain(TrainId trainId) const;
    // 編成から置いた列車の一覧。
    std::vector<TrainId> TrainsOfFormation(const Guid& formationId) const;
    // 編成から置いた列車が1本でもあるか。
    bool HasTrainOfFormation(const Guid& formationId) const;
    const std::unordered_map<TrainId, Guid, GuidHash>& TrainFormationLinks() const;

    // 列車の配置元の編成のタグ。編成が未登録・削除済みなら空。
    const std::vector<int>& TrainTags(TrainId trainId) const;

    // 編成が消えるとき: その編成のタグ一覧と、その編成を指す列車の対応を消す。
    void RemoveFormation(const Guid& formationId);
    void RemoveTrain(TrainId trainId);

private:
    std::vector<TrainTagDef> tags_;
    int nextTagId_ = 0;
    std::unordered_map<Guid, std::vector<int>, GuidHash> formationTagIds_;
    std::unordered_map<TrainId, Guid, GuidHash> trainFormation_;
};

}  // namespace rs::world
