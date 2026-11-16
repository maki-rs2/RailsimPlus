//
// PointRoutineLayerAction.cpp - 分岐のルーチン表のアプリサービスのうち、層(編成・タグ)の追加・削除・並べ替え
//
// 層は、ルーチンの項目と条件の項目の和集合で成り立つ。どちらか一方しか持たない層(読み込んだデータなど)もある。
//
#include "PointRoutineAction.h"

#include <algorithm>
#include <utility>

#include "PointRoutineChange.h"

namespace rs::app {

using namespace point_routine_change;
using namespace routine_list_edit;

namespace {

// タグの層の並び。ルーチンの順に並べ、条件にだけある層をその後ろへ続ける。
std::vector<int> TagLayerOrder(const rs::world::TrackNode& node) {
    std::vector<int> order;
    for (const rs::world::PointTagRoutine& entry : node.pointTagRoutines) order.push_back(entry.tagId);
    for (const rs::world::PointTagCondition& entry : node.pointTagConditions) {
        if (std::find(order.begin(), order.end(), entry.tagId) == order.end()) order.push_back(entry.tagId);
    }
    return order;
}

// order での位置が小さい順に entries を並べ直す(order に無い項目は末尾)。
template <typename Entry>
void SortByTagOrder(std::vector<Entry>& entries, const std::vector<int>& order) {
    const auto position = [&order](int tagId) { return std::find(order.begin(), order.end(), tagId) - order.begin(); };
    std::stable_sort(entries.begin(), entries.end(), [&position](const Entry& left, const Entry& right) {
        return position(left.tagId) < position(right.tagId);
    });
}

} // namespace

bool PointRoutineAction::AddFormationLayer(const rs::world::Guid& formationId) {
    const rs::world::TrackNode* node = FindPoint(editor_, nodeId_);
    if (node == nullptr) return false;
    std::vector<rs::world::PointFormationRoutine> updated = node->pointFormationRoutines;
    // 追加した層は、既定値の要素を1つ持った状態で始める(空の層にしない)。
    if (!AddIfAbsent(updated, &rs::world::PointFormationRoutine::formationId, formationId,
                     rs::world::PointFormationRoutine{formationId, std::vector<rs::world::PointRoutineElement>(1)})) {
        return false;
    }
    rs::editor::PointDiaDelta delta;
    delta.formationRoutines = std::move(updated);
    ExecutePointDiaChange(editor_, nodeId_, std::move(delta));
    return true;
}

bool PointRoutineAction::AddTagLayer(int tagId) {
    const rs::world::TrackNode* node = FindPoint(editor_, nodeId_);
    if (node == nullptr) return false;
    std::vector<rs::world::PointTagRoutine> updated = node->pointTagRoutines;
    if (!AddIfAbsent(updated, &rs::world::PointTagRoutine::tagId, tagId,
                     rs::world::PointTagRoutine{tagId, std::vector<rs::world::PointRoutineElement>(1)})) {
        return false;
    }
    rs::editor::PointDiaDelta delta;
    delta.tagRoutines = std::move(updated);
    ExecutePointDiaChange(editor_, nodeId_, std::move(delta));
    return true;
}

void PointRoutineAction::RemoveLayer(const PointLayerRef& layer) {
    const rs::world::TrackNode* node = FindPoint(editor_, nodeId_);
    if (node == nullptr) return;
    rs::editor::PointDiaDelta delta;
    if (layer.kind == PointLayerRef::Kind::Formation) {
        std::vector<rs::world::PointFormationRoutine> routines = node->pointFormationRoutines;
        std::vector<rs::world::PointFormationCondition> conditions = node->pointFormationConditions;
        std::erase_if(routines, [&layer](const auto& entry) { return entry.formationId == layer.formationId; });
        std::erase_if(conditions, [&layer](const auto& entry) { return entry.formationId == layer.formationId; });
        delta.formationRoutines = std::move(routines);
        delta.formationConditions = std::move(conditions);
    } else if (layer.kind == PointLayerRef::Kind::Tag) {
        std::vector<rs::world::PointTagRoutine> routines = node->pointTagRoutines;
        std::vector<rs::world::PointTagCondition> conditions = node->pointTagConditions;
        std::erase_if(routines, [&layer](const auto& entry) { return entry.tagId == layer.tagId; });
        std::erase_if(conditions, [&layer](const auto& entry) { return entry.tagId == layer.tagId; });
        delta.tagRoutines = std::move(routines);
        delta.tagConditions = std::move(conditions);
    } else {
        return; // 全列車共通の層は消せない
    }
    ExecutePointDiaChange(editor_, nodeId_, std::move(delta));
}

void PointRoutineAction::MoveTagLayerTo(int fromTagId, int toTagId) {
    const rs::world::TrackNode* node = FindPoint(editor_, nodeId_);
    if (node == nullptr) return;
    std::vector<int> order = TagLayerOrder(*node);
    const auto from = std::find(order.begin(), order.end(), fromTagId);
    const auto to = std::find(order.begin(), order.end(), toTagId);
    if (from == order.end() || to == order.end()) return;
    if (!MoveWithin(order, static_cast<std::size_t>(from - order.begin()), static_cast<std::size_t>(to - order.begin()))) return;
    // 走行側はルーチンと条件を別々の一覧の順に調べるので、両方を同じ順に直す。
    std::vector<rs::world::PointTagRoutine> routines = node->pointTagRoutines;
    std::vector<rs::world::PointTagCondition> conditions = node->pointTagConditions;
    SortByTagOrder(routines, order);
    SortByTagOrder(conditions, order);
    rs::editor::PointDiaDelta delta;
    delta.tagRoutines = std::move(routines);
    delta.tagConditions = std::move(conditions);
    ExecutePointDiaChange(editor_, nodeId_, std::move(delta));
}

} // namespace rs::app
