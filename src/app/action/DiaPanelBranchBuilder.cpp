//
// DiaPanelBranchBuilder.cpp
//
#include "DiaPanelBranchBuilder.h"

#include <algorithm>
#include <limits>

#include "../workspace/DiaPanelFormat.h"
#include "../workspace/RailLabelPresentation.h"
#include "../../world/alignment/PointHandLinks.h"

namespace rs::app::dia_builder {
namespace {

using namespace workspace;
using rs::world::PointCondition;
using rs::world::PointRoutineElement;

DiaPanelElement MakeBranchElement(const PointRoutineElement& source, bool next) {
    DiaPanelElement element;
    element.hand_ = FormatPointHand(source.hand, source.random);
    element.summary_ = element.hand_ + (source.enabled ? "" : " (無効)");
    element.disabled_ = !source.enabled;
    element.next_ = next;
    return element;
}

std::string ConditionSummary(const PointCondition& condition) {
    const std::string hand = FormatPointHand(condition.hand);
    if (condition.kind == PointCondition::Kind::TimeRange) {
        return FormatDiaTime(condition.startTime) + "-" + FormatDiaTime(condition.endTime) + " → " + hand;
    }
    const std::string count =
        condition.blocks.empty() ? "閉塞なし" : "閉塞" + std::to_string(condition.blocks.size()) + "つ";
    return count + " " + (condition.requireAllOccupied ? "すべて" : "いずれか") + " → " + hand;
}

// 閉塞の候補。全閉塞を、分岐に最も近いレールのスプライン点までの距離が近い順に並べる。
std::vector<DiaPanelChoice> BlockChoicesNearestFirst(const rs::world::TrackGraph& graph,
                                                     const rs::world::TrackNode& node) {
    struct Entry {
        float distanceSquared;
        std::size_t index;
    };
    std::vector<Entry> entries;
    for (std::size_t index = 0; index < graph.blocks.size(); ++index) {
        float best = std::numeric_limits<float>::max();
        for (const rs::world::Guid& edgeId : graph.blocks[index].edges) {
            const auto edge = graph.edges.find(edgeId);
            if (edge == graph.edges.end()) continue;
            for (const rs::world::TrackNodeSample& sample : edge->second.spline) {
                const float dx = sample.pos.x - node.pos.x;
                const float dy = sample.pos.y - node.pos.y;
                const float dz = sample.pos.z - node.pos.z;
                best = std::min(best, dx * dx + dy * dy + dz * dz);
            }
        }
        entries.push_back({best, index});
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& left, const Entry& right) { return left.distanceSquared < right.distanceSquared; });
    std::vector<DiaPanelChoice> choices;
    for (const Entry& entry : entries) {
        const rs::world::Block& block = graph.blocks[entry.index];
        choices.push_back({BlockDisplayName(block, entry.index), false, 0, block.guid});
    }
    return choices;
}

DiaPanelCondition MakeCondition(const PointCondition& source, const std::vector<DiaPanelChoice>& blockChoices) {
    DiaPanelCondition condition;
    condition.kind_ = source.kind == PointCondition::Kind::TimeRange ? "time" : "block";
    condition.summary_ = ConditionSummary(source);
    condition.disabled_ = !source.enabled;
    condition.hand_ = FormatPointHand(source.hand);
    condition.all_ = source.requireAllOccupied;
    if (source.kind == PointCondition::Kind::TimeRange) {
        condition.start_ = FormatDiaTime(source.startTime);
        condition.end_ = FormatDiaTime(source.endTime);
        return condition;
    }
    condition.blocks_ = blockChoices;
    for (DiaPanelChoice& choice : condition.blocks_) {
        choice.selected_ = std::find(source.blocks.begin(), source.blocks.end(), choice.id_) != source.blocks.end();
    }
    return condition;
}

struct LayerSource {
    const std::vector<PointRoutineElement>* routine = nullptr;
    const std::vector<PointCondition>* conditions = nullptr;
};

// 閉塞の候補は距離の計算が重いので、閉塞の在線の条件があるときだけ、分岐ごとに1回作る。
struct BlockChoiceCache {
    const rs::world::TrackGraph& graph;
    const rs::world::TrackNode& node;
    std::vector<DiaPanelChoice> choices;
    bool built = false;

    const std::vector<DiaPanelChoice>& Get() {
        if (!built) {
            choices = BlockChoicesNearestFirst(graph, node);
            built = true;
        }
        return choices;
    }
};

DiaPanelLayer MakeBranchLayer(DiaPanelLayer base, const LayerSource& source, BlockChoiceCache& cache) {
    bool nextFound = false;
    if (source.routine != nullptr) {
        for (const PointRoutineElement& element : *source.routine) {
            const bool next = element.enabled && !nextFound;
            nextFound = nextFound || next;
            base.elements_.push_back(MakeBranchElement(element, next));
        }
    }
    if (source.conditions != nullptr) {
        static const std::vector<DiaPanelChoice> kNoChoices;
        for (const PointCondition& condition : *source.conditions) {
            const bool needsBlocks = condition.kind == PointCondition::Kind::BlockOccupancy;
            base.conditions_.push_back(MakeCondition(condition, needsBlocks ? cache.Get() : kNoChoices));
        }
    }
    return base;
}

template <typename Entry, typename Key>
const Entry* FindEntry(const std::vector<Entry>& entries, Key Entry::*member, const Key& key) {
    for (const Entry& entry : entries) {
        if (entry.*member == key) return &entry;
    }
    return nullptr;
}

template <typename Item>
bool AnyEnabled(const std::vector<Item>& items) {
    return std::any_of(items.begin(), items.end(), [](const Item& item) { return item.enabled; });
}

template <typename Entry>
LayerSource SourceOf(const Entry* routine, const std::vector<PointCondition>* conditions) {
    return {routine == nullptr ? nullptr : &routine->routine, conditions};
}

} // namespace

std::vector<PointEntry> SortedPoints(const rs::world::TrackGraph& graph) {
    std::vector<PointEntry> points;
    for (const auto& [id, node] : graph.nodes) {
        if (node.IsPoint()) points.push_back({id, &node});
    }
    std::sort(points.begin(), points.end(), [](const PointEntry& left, const PointEntry& right) {
        if (left.node->pos.x != right.node->pos.x) return left.node->pos.x < right.node->pos.x;
        if (left.node->pos.z != right.node->pos.z) return left.node->pos.z < right.node->pos.z;
        return left.id < right.id;
    });
    return points;
}

std::string PointDisplayName(const rs::world::TrackNode& node) {
    return node.pointName.empty() ? "分岐" : node.pointName;
}

bool PointIsUnset(const rs::world::TrackNode& node) {
    if (AnyEnabled(node.pointRoutine) || AnyEnabled(node.pointConditions)) return false;
    for (const auto& entry : node.pointFormationRoutines) if (AnyEnabled(entry.routine)) return false;
    for (const auto& entry : node.pointTagRoutines) if (AnyEnabled(entry.routine)) return false;
    for (const auto& entry : node.pointFormationConditions) if (AnyEnabled(entry.conditions)) return false;
    for (const auto& entry : node.pointTagConditions) if (AnyEnabled(entry.conditions)) return false;
    return true;
}

DiaPanelTarget MakeBranchTarget(const DiaPanelBuildInput& input, const rs::world::TrackGraph& graph,
                                const PointEntry& point, std::string breadcrumb, int sceneIndex) {
    const rs::world::TrackNode& node = *point.node;
    DiaPanelTarget target;
    target.breadcrumb_ = std::move(breadcrumb);
    target.branch_ = true;
    target.nodeId_ = point.id;
    target.sceneIndex_ = sceneIndex;
    const std::vector<rs::world::PointSideLinks> sides = rs::world::BuildPointSideLinks(graph, node);
    for (std::size_t index = 0; index < sides.size(); ++index) {
        const std::string label = sides.size() == 1 ? "開通先" : "開通先" + std::to_string(index + 1);
        target.openings_.push_back({label, sides[index].leftActive, sides[index].rightActive});
    }
    BlockChoiceCache cache{graph, node, {}, false};
    // 層は、ルーチンか条件のどちらかがあれば出す(ルーチンの並びが先、条件だけの層はその後ろ)。
    std::vector<rs::world::Guid> formations;
    for (const auto& entry : node.pointFormationRoutines) formations.push_back(entry.formationId);
    for (const auto& entry : node.pointFormationConditions) {
        if (std::find(formations.begin(), formations.end(), entry.formationId) == formations.end()) {
            formations.push_back(entry.formationId);
        }
    }
    for (const rs::world::Guid& formationId : formations) {
        DiaPanelLayer base;
        base.name_ = FormationName(input, formationId);
        base.kind_ = "編成";
        base.deletable_ = true;
        base.routineKey_ = kDiaFormationLayerKey;
        base.formationId_ = formationId;
        const rs::world::PointFormationRoutine* routine =
            FindEntry(node.pointFormationRoutines, &rs::world::PointFormationRoutine::formationId, formationId);
        const rs::world::PointFormationCondition* conditions =
            FindEntry(node.pointFormationConditions, &rs::world::PointFormationCondition::formationId, formationId);
        target.layers_.push_back(MakeBranchLayer(
            std::move(base), SourceOf(routine, conditions == nullptr ? nullptr : &conditions->conditions), cache));
    }
    std::vector<int> tags;
    for (const auto& entry : node.pointTagRoutines) tags.push_back(entry.tagId);
    for (const auto& entry : node.pointTagConditions) {
        if (std::find(tags.begin(), tags.end(), entry.tagId) == tags.end()) tags.push_back(entry.tagId);
    }
    for (const int tagId : tags) {
        DiaPanelLayer base;
        base.name_ = TagLabel(input, tagId);
        base.kind_ = "タグ";
        base.deletable_ = true;
        base.reorderable_ = true;
        base.routineKey_ = rs::world::TagRoutineLayerKey(tagId);
        const rs::world::PointTagRoutine* routine = FindEntry(node.pointTagRoutines, &rs::world::PointTagRoutine::tagId, tagId);
        const rs::world::PointTagCondition* conditions =
            FindEntry(node.pointTagConditions, &rs::world::PointTagCondition::tagId, tagId);
        target.layers_.push_back(MakeBranchLayer(
            std::move(base), SourceOf(routine, conditions == nullptr ? nullptr : &conditions->conditions), cache));
    }
    DiaPanelLayer common;
    common.name_ = "全列車共通";
    common.kind_ = "全列車共通";
    common.routineKey_ = rs::world::kCommonRoutineLayerKey;
    target.layers_.push_back(MakeBranchLayer(std::move(common), {&node.pointRoutine, &node.pointConditions}, cache));
    return target;
}

} // namespace rs::app::dia_builder
