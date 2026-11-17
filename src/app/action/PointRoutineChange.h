//
// PointRoutineChange.h - PointRoutineAction の実装ファイル間で共有する、分岐の層を書き換える補助
//
#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "PointRoutineAction.h"
#include "RoutineListEdit.h"
#include "../../editor/tool/PointDiaCommand.h"
#include "../../editor/tool/Editor.h"

namespace rs::app::point_routine_change {

inline void ExecutePointDiaChange(rs::editor::Editor& editor, const rs::world::Guid& nodeId,
                                  rs::editor::PointDiaDelta delta) {
    editor.ExecutePlatformCommand(std::make_unique<rs::editor::SetPointDiaCommand>(nodeId, std::move(delta)));
}

inline const rs::world::TrackNode* FindPoint(const rs::editor::Editor& editor, const rs::world::Guid& nodeId) {
    const auto found = editor.graph.nodes.find(nodeId);
    return found != editor.graph.nodes.end() && found->second.IsPoint() ? &found->second : nullptr;
}

// ルーチン(PointRoutineElement)の層と、条件(PointCondition)の層は、持ち主のキーも並べ方も同じ。
// 型とメンバの対応を種類ごとに1か所へまとめ、層の探し方・書き換え方を1つの関数で書けるようにする。
struct RoutineSlots {
    using Item = rs::world::PointRoutineElement;
    using FormationEntry = rs::world::PointFormationRoutine;
    using TagEntry = rs::world::PointTagRoutine;
    static const std::vector<Item>& Common(const rs::world::TrackNode& node) { return node.pointRoutine; }
    static const std::vector<FormationEntry>& Formations(const rs::world::TrackNode& node) { return node.pointFormationRoutines; }
    static const std::vector<TagEntry>& Tags(const rs::world::TrackNode& node) { return node.pointTagRoutines; }
    static std::vector<Item>& Items(FormationEntry& entry) { return entry.routine; }
    static std::vector<Item>& Items(TagEntry& entry) { return entry.routine; }
    static const std::vector<Item>& Items(const FormationEntry& entry) { return entry.routine; }
    static const std::vector<Item>& Items(const TagEntry& entry) { return entry.routine; }
    static void SetCommon(rs::editor::PointDiaDelta& delta, std::vector<Item> value) { delta.routine = std::move(value); }
    static void SetFormations(rs::editor::PointDiaDelta& delta, std::vector<FormationEntry> value) { delta.formationRoutines = std::move(value); }
    static void SetTags(rs::editor::PointDiaDelta& delta, std::vector<TagEntry> value) { delta.tagRoutines = std::move(value); }
};

struct ConditionSlots {
    using Item = rs::world::PointCondition;
    using FormationEntry = rs::world::PointFormationCondition;
    using TagEntry = rs::world::PointTagCondition;
    static const std::vector<Item>& Common(const rs::world::TrackNode& node) { return node.pointConditions; }
    static const std::vector<FormationEntry>& Formations(const rs::world::TrackNode& node) { return node.pointFormationConditions; }
    static const std::vector<TagEntry>& Tags(const rs::world::TrackNode& node) { return node.pointTagConditions; }
    static std::vector<Item>& Items(FormationEntry& entry) { return entry.conditions; }
    static std::vector<Item>& Items(TagEntry& entry) { return entry.conditions; }
    static const std::vector<Item>& Items(const FormationEntry& entry) { return entry.conditions; }
    static const std::vector<Item>& Items(const TagEntry& entry) { return entry.conditions; }
    static void SetCommon(rs::editor::PointDiaDelta& delta, std::vector<Item> value) { delta.conditions = std::move(value); }
    static void SetFormations(rs::editor::PointDiaDelta& delta, std::vector<FormationEntry> value) { delta.formationConditions = std::move(value); }
    static void SetTags(rs::editor::PointDiaDelta& delta, std::vector<TagEntry> value) { delta.tagConditions = std::move(value); }
};

// layer の項目列(Slots が指す種類)のコピーを change で書き換え、1コマンドで反映する。
// 編成・タグの層に項目がなければ、createMissing のときだけ空で作る(条件だけの層へ要素を足すときなど)。
template <typename Slots, typename Change>
void ChangeLayerItems(rs::editor::Editor& editor, const rs::world::Guid& nodeId, const PointLayerRef& layer,
                      bool createMissing, Change&& change) {
    const rs::world::TrackNode* node = FindPoint(editor, nodeId);
    if (node == nullptr) return;
    rs::editor::PointDiaDelta delta;
    if (layer.kind == PointLayerRef::Kind::Common) {
        std::vector<typename Slots::Item> items = Slots::Common(*node);
        change(items);
        Slots::SetCommon(delta, std::move(items));
    } else if (layer.kind == PointLayerRef::Kind::Formation) {
        std::vector<typename Slots::FormationEntry> entries = Slots::Formations(*node);
        typename Slots::FormationEntry* entry =
            routine_list_edit::FindByKey(entries, &Slots::FormationEntry::formationId, layer.formationId);
        if (entry == nullptr) {
            if (!createMissing) return;
            entries.push_back({layer.formationId, {}});
            entry = &entries.back();
        }
        change(Slots::Items(*entry));
        Slots::SetFormations(delta, std::move(entries));
    } else {
        std::vector<typename Slots::TagEntry> entries = Slots::Tags(*node);
        typename Slots::TagEntry* entry = routine_list_edit::FindByKey(entries, &Slots::TagEntry::tagId, layer.tagId);
        if (entry == nullptr) {
            if (!createMissing) return;
            entries.push_back({layer.tagId, {}});
            entry = &entries.back();
        }
        change(Slots::Items(*entry));
        Slots::SetTags(delta, std::move(entries));
    }
    ExecutePointDiaChange(editor, nodeId, std::move(delta));
}

} // namespace rs::app::point_routine_change
