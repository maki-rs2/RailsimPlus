#include "PointRoutineResolve.h"

#include <algorithm>
#include <utility>

#include "PointRoutine.h"
#include "RailLinkOverlay.h"
#include "TrackEdge.h"
#include "TrackGraph.h"

namespace rs::world {
namespace {

// node・exitSideについての決定をoverrideから引く。無ければnullopt。
std::optional<int> FindDecision(const NodeSlotOverride* decisions, const Guid& nodeGuid,
                                int exitSide) {
    if (decisions == nullptr) return std::nullopt;
    const NodeSlotOverride::const_iterator it = decisions->find(nodeGuid);
    if (it == decisions->end()) return std::nullopt;
    return it->second[static_cast<size_t>(exitSide)];
}

// enteredEdgeからnodeへ入ったときの出口の面と、その面の分岐数。enteredEdgeがnodeに
// つながっていなければnullopt。
struct PointExit {
    int exitSide = 0;
    int count = 0;
};
std::optional<PointExit> ResolvePointExit(const TrackNode& node, const Guid& enteredEdge) {
    for (const TrackNode::Link& link : node.links) {
        if (link.edge != enteredEdge) continue;
        PointExit exit;
        exit.exitSide = 1 - link.side;
        exit.count = node.LinkCountOnSide(exit.exitSide);
        return exit;
    }
    return std::nullopt;
}

struct PointRoutineScanResult {
    double remaining = 0.0;
    Guid endNode = NilGuid();
    bool reachedSceneEnd = false;
};

// previousDecisionsが非nullの場合、node(sharedNode)のexitSide面について既に決定が
// あれば(std::nulloptでなければ)、activeSlotの解決・カーソル消費を行わずその決定値を
// 使う(既に決定済みとして扱う)。commitToActiveSlotがtrueなら決定値をnode.activeSlotへ
// 書き込む(物理通過パス用)。falseなら書き込まない(先読み専用。共有activeSlotは
// 変更しない)。decisionsOutが非nullの場合、通過した分岐(count>=2でoccupied隣接で
// ないnode)全てについて、trailing(条件/ルーチンとも該当なし)でも現在のactiveSlotの
// 値をそのままコピーして(node.guid, exitSide)エントリを書き込む(後続の読み取り専用
// スキャンがoverrideなしの生activeSlotへフォールバックして古い状態を再び読むのを
// 防ぐため)。呼び出し側がこのwalk自身の以降の分岐選択に使うoverrideとしても使える
// よう、decisionsOutへの書き込み直後にSelectNextRealLinkへ渡す。
// 在線ガードが掛かった分岐(count>=2でoccupied隣接のnode)ではカーソルを消費せず、
// activeSlotもdecisionsOutも書かない。かわりに、前回までに決めた開通先
// (previousDecisions、なければpreviousHeld)があればheldOutへ写して預かる
// (ガードが外れたときに同じ決定を使い、ルーチンを消費し直さないため)。
// ガードが掛かっていない分岐では、previousDecisions、previousHeldの順に決定を探し、
// どちらにも無いときだけ条件、ルーチンの順で解決する。
PointRoutineScanResult ApplyPointRoutinesInScene(
    TrackGraph& graph, const TrackPosition& start, double ds,
    PointRoutineCursors& routineCursors,
    const std::unordered_set<Guid, GuidHash>& occupiedBlocks,
    const PointConditionContext& conditionContext, int& totalHops,
    const NodeSlotOverride* previousDecisions, const NodeSlotOverride* previousHeld,
    bool commitToActiveSlot, NodeSlotOverride* decisionsOut, NodeSlotOverride* heldOut) {
    TrackPosition position = start;
    PointRoutineScanResult result;
    result.remaining = std::max(ds, 0.0);
    constexpr int kMaxHops = 4096;

    for (; totalHops <= kMaxHops; ++totalHops) {
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
            graph.edges.find(position.edge);
        if (edgeIt == graph.edges.end()) return result;
        const TrackEdge& edge = edgeIt->second;
        const double edgeLength = EdgeLength(edge);
        const double toEnd = position.direction >= 0 ? edgeLength - position.s : position.s;
        if (result.remaining <= toEnd) return result;
        result.remaining -= toEnd;

        const Guid sharedNode = LinkNodeOfEnd(graph, edge, position.direction >= 0 ? 1 : 0);
        TrackNode* nodePtr = FindLinkNodeMutable(graph, sharedNode);
        if (nodePtr == nullptr) return result;
        TrackNode& node = *nodePtr;

        const std::optional<PointExit> pointExit = ResolvePointExit(node, position.edge);
        if (!pointExit.has_value()) return result;
        const int exitSide = pointExit->exitSide;
        const int count = pointExit->count;
        if (count >= 2 && IsNodeAdjacentToOccupiedBlock(graph, node, occupiedBlocks)) {
            if (heldOut != nullptr) {
                std::optional<int> held = FindDecision(previousDecisions, sharedNode, exitSide);
                if (!held.has_value()) held = FindDecision(previousHeld, sharedNode, exitSide);
                if (held.has_value()) {
                    (*heldOut)[sharedNode][static_cast<size_t>(exitSide)] = held;
                }
            }
        } else if (count >= 2) {
            std::optional<int> decidedSlot = FindDecision(previousDecisions, sharedNode, exitSide);
            if (!decidedSlot.has_value()) {
                decidedSlot = FindDecision(previousHeld, sharedNode, exitSide);
            }
            if (!decidedSlot.has_value()) {
                const std::optional<int> conditionHand = ResolvePointConditionHand(
                    node, conditionContext.trainId, conditionContext.formationId,
                    *conditionContext.trainTagIds, *conditionContext.previousOccupiedBlocks, conditionContext.nowAbsSeconds);
                if (conditionHand.has_value()) {
                    decidedSlot = ResolveHandToOrdinal(graph, node, exitSide, *conditionHand);
                    // ルーチンのカーソルは消費しない(条件が勝ったので未使用のまま。
                    // ポイント在線ガードと同じ理由: 消費すると転換していないのに順番だけ進む)
                } else {
                    const ResolvedPointRoutine resolved = ResolvePointTrainRoutine(
                        node.pointTrainRoutines, node.pointFormationRoutines,
                        node.pointTagRoutines, node.pointRoutine, conditionContext.trainId,
                        conditionContext.formationId, *conditionContext.trainTagIds);
                    // ルーチンが空の層ではカーソルを作らない(ポイントを通過するたびに
                    // 空のカーソル項目が増えるのを避ける)。
                    if (!resolved.routine->empty()) {
                        int& cursor = !GuidIsNil(resolved.trainId)
                            ? routineCursors.trainCursors[sharedNode][resolved.trainId]
                            : routineCursors.layerCursors[sharedNode][resolved.layerKey];
                        const PointRoutineElement* element =
                            DequeuePointRoutineElement(*resolved.routine, cursor);
                        if (element != nullptr) {
                            const int hand = element->random
                                ? static_cast<int>(routineCursors.rng() & 1u)
                                : element->hand;
                            decidedSlot = ResolveHandToOrdinal(graph, node, exitSide, hand);
                        }
                    }
                }
                // 条件もルーチンも該当なし: trailing。decidedSlotは未設定のまま
                // (以下で現在のactiveSlotの値へフォールバックする)。
            }
            const int finalSlot =
                decidedSlot.value_or(node.activeSlot[static_cast<size_t>(exitSide)]);
            if (commitToActiveSlot) node.activeSlot[static_cast<size_t>(exitSide)] = finalSlot;
            if (decisionsOut != nullptr) {
                (*decisionsOut)[sharedNode][static_cast<size_t>(exitSide)] = finalSlot;
            }
        }

        const LinkSelection selection =
            SelectNextRealLink(graph, node, position.edge, decisionsOut);
        if (!selection.next.has_value()) {
            result.endNode = sharedNode;
            result.reachedSceneEnd = true;
            return result;
        }
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator nextEdgeIt =
            graph.edges.find(selection.next->edge);
        if (nextEdgeIt == graph.edges.end()) return result;
        position.edge = selection.next->edge;
        if (selection.next->end == 0) {
            position.s = 0.0;
            position.direction = +1;
        } else {
            position.s = EdgeLength(nextEdgeIt->second);
            position.direction = -1;
        }
    }
    return result;
}

// warp境界を越えてApplyPointRoutinesInSceneを繰り返す共通コア。
// previousDecisions/previousHeld/commitToActiveSlot/decisionsOut/heldOutは
// ApplyPointRoutinesInSceneへ
// そのまま渡す(意味は同関数の説明を参照)。
void ApplyPointRoutinesAcrossScenesCore(
    const std::function<TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    size_t startScene, const TrackPosition& start, double ds,
    PointRoutineCursors& routineCursors,
    const std::unordered_set<Guid, GuidHash>& occupiedBlocks,
    const PointConditionContext& conditionContext,
    const NodeSlotOverride* previousDecisions, const NodeSlotOverride* previousHeld,
    bool commitToActiveSlot, NodeSlotOverride* decisionsOut, NodeSlotOverride* heldOut) {
    size_t sceneIndex = startScene;
    TrackPosition position = start;
    double remaining = std::max(ds, 0.0);
    int totalHops = 0;
    constexpr int kMaxWarpHops = 64;

    for (int warpHop = 0; warpHop <= kMaxWarpHops; ++warpHop) {
        TrackGraph* graph = graphForScene(sceneIndex);
        if (graph == nullptr) return;
        const PointRoutineScanResult step = ApplyPointRoutinesInScene(
            *graph, position, remaining, routineCursors, occupiedBlocks, conditionContext,
            totalHops, previousDecisions, previousHeld, commitToActiveSlot, decisionsOut,
            heldOut);
        if (!step.reachedSceneEnd) return;

        const std::optional<WarpEndpoint> other = warpTable.Find(sceneIndex, step.endNode);
        if (!other.has_value()) return;
        TrackGraph* otherGraph = graphForScene(other->sceneIndex);
        if (otherGraph == nullptr) return;
        const TrackNode* otherNode = FindLinkNode(*otherGraph, other->node);
        if (otherNode == nullptr || otherNode->links.empty()) return;
        const TrackNode::Link& nextLink = otherNode->links[0];
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator nextEdgeIt =
            otherGraph->edges.find(nextLink.edge);
        if (nextEdgeIt == otherGraph->edges.end()) return;

        sceneIndex = other->sceneIndex;
        position.edge = nextLink.edge;
        if (nextLink.end == 0) {
            position.s = 0.0;
            position.direction = +1;
        } else {
            position.s = EdgeLength(nextEdgeIt->second);
            position.direction = -1;
        }
        remaining = step.remaining;
    }
}

}  // namespace

bool IsNodeAdjacentToOccupiedBlock(const TrackGraph& graph, const TrackNode& node,
                                   const std::unordered_set<Guid, GuidHash>& occupiedBlocks) {
    for (const TrackNode::Link& link : node.links) {
        const std::unordered_map<Guid, std::vector<size_t>, GuidHash>::const_iterator blockIt =
            graph.edgeToBlocks.find(link.edge);
        if (blockIt == graph.edgeToBlocks.end()) continue;
        for (size_t blockIndex : blockIt->second) {
            if (occupiedBlocks.contains(graph.blocks[blockIndex].guid)) return true;
        }
    }
    return false;
}

void PointRoutineCursors::Clear() {
    layerCursors.clear();
    trainCursors.clear();
    seekDecisions.clear();
    heldDecisions.clear();
    rng.seed(kRandomSeed);
}

void PointRoutineCursors::RemoveTrain(TrainId trainId) {
    for (auto& [node, cursors] : trainCursors) {
        (void)node;
        cursors.erase(trainId);
    }
    seekDecisions.erase(trainId);
    heldDecisions.erase(trainId);
}

void ApplyPointRoutinesAhead(TrackGraph& graph, const TrackPosition& start, double ds,
                             PointRoutineCursors& routineCursors,
                             const std::unordered_set<Guid, GuidHash>& occupiedBlocks) {
    static const std::vector<int> kNoTags;
    static const std::unordered_set<Guid, GuidHash> kNoPreviousBlocks;
    const PointConditionContext context{NilGuid(), &kNoTags, &kNoPreviousBlocks, 0.0};
    ApplyPointRoutinesAhead(graph, start, ds, routineCursors, occupiedBlocks, context);
}

void ApplyPointRoutinesAhead(TrackGraph& graph, const TrackPosition& start, double ds,
                             PointRoutineCursors& routineCursors,
                             const std::unordered_set<Guid, GuidHash>& occupiedBlocks,
                             const PointConditionContext& conditionContext) {
    int totalHops = 0;
    ApplyPointRoutinesInScene(graph, start, ds, routineCursors, occupiedBlocks, conditionContext,
                              totalHops, nullptr, nullptr, /*commitToActiveSlot=*/true, nullptr,
                              nullptr);
}

void ApplyPointRoutinesAheadAcrossScenes(
    const std::function<TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    size_t startScene, const TrackPosition& start, double ds,
    PointRoutineCursors& routineCursors,
    const std::unordered_set<Guid, GuidHash>& occupiedBlocks,
    const PointConditionContext& conditionContext) {
    // 物理通過パス: seek集合(seekDecisions)による確定値を共有activeSlotへ書き込む
    // (just-in-time commit)。既に決定済みの分岐は再Dequeueせず(span構築時に確定した
    // 経路を物理通過時に上書きしないため)、その決定値をそのままactiveSlotへコミットする。
    // seekDecisionsに無い分岐(先読みが間に合わなかった場合のフォールバック)は
    // その場でDequeueする。
    const std::unordered_map<TrainId, NodeSlotOverride, GuidHash>::const_iterator seekIt =
        routineCursors.seekDecisions.find(conditionContext.trainId);
    const NodeSlotOverride* previousDecisions =
        seekIt != routineCursors.seekDecisions.end() ? &seekIt->second : nullptr;
    const std::unordered_map<TrainId, NodeSlotOverride, GuidHash>::const_iterator heldIt =
        routineCursors.heldDecisions.find(conditionContext.trainId);
    const NodeSlotOverride* previousHeld =
        heldIt != routineCursors.heldDecisions.end() ? &heldIt->second : nullptr;
    ApplyPointRoutinesAcrossScenesCore(graphForScene, warpTable, startScene, start, ds,
                                       routineCursors, occupiedBlocks, conditionContext,
                                       previousDecisions, previousHeld,
                                       /*commitToActiveSlot=*/true, nullptr, nullptr);
}

void ResolveSeekAheadPointRoutines(
    const std::function<TrackGraph*(size_t)>& graphForScene, const WarpLinkTable& warpTable,
    TrainId trainId, size_t startScene, const TrackPosition& start, double ds,
    PointRoutineCursors& routineCursors,
    const std::unordered_set<Guid, GuidHash>& occupiedBlocks,
    const PointConditionContext& conditionContext) {
    const std::unordered_map<TrainId, NodeSlotOverride, GuidHash>::const_iterator seekIt =
        routineCursors.seekDecisions.find(trainId);
    const NodeSlotOverride* previousDecisions =
        seekIt != routineCursors.seekDecisions.end() ? &seekIt->second : nullptr;
    const std::unordered_map<TrainId, NodeSlotOverride, GuidHash>::const_iterator heldIt =
        routineCursors.heldDecisions.find(trainId);
    const NodeSlotOverride* previousHeld =
        heldIt != routineCursors.heldDecisions.end() ? &heldIt->second : nullptr;

    // 重要: 先読みは共有activeSlotを一切書き込まない(commitToActiveSlot=false)。
    // 列車ごとのnewDecisionsだけを更新し、経路のプレビューに留める。
    NodeSlotOverride newDecisions;
    NodeSlotOverride newHeld;
    ApplyPointRoutinesAcrossScenesCore(graphForScene, warpTable, startScene, start, ds,
                                       routineCursors, occupiedBlocks, conditionContext,
                                       previousDecisions, previousHeld,
                                       /*commitToActiveSlot=*/false, &newDecisions, &newHeld);

    // 今回のwalkで到達した分岐の決定で丸ごと置き換える(前回pass分は破棄)。
    // ガード中の分岐の決定はseekDecisionsへ入れず(span経路と実際の走行がずれるため)、
    // heldDecisionsへ預ける。
    routineCursors.seekDecisions[trainId] = std::move(newDecisions);
    routineCursors.heldDecisions[trainId] = std::move(newHeld);
}

std::optional<double> FindGuardedPointStopAheadM(
    const std::function<const TrackGraph*(size_t)>& graphForScene, const ForwardScanSpan& span,
    const std::unordered_set<Guid, GuidHash>& occupiedBlocks) {
    if (occupiedBlocks.empty()) return std::nullopt;
    for (size_t index = 0; index + 1 < span.entries.size(); ++index) {
        const EdgeAheadAcrossScenes& entry = span.entries[index];
        const EdgeAheadAcrossScenes& next = span.entries[index + 1];
        // warpをまたぐ境目は分岐ではない。
        if (entry.warpHops != next.warpHops || entry.sceneIndex != next.sceneIndex) continue;
        const TrackGraph* graph = graphForScene(entry.sceneIndex);
        if (graph == nullptr) continue;
        const std::unordered_map<Guid, TrackEdge, GuidHash>::const_iterator edgeIt =
            graph->edges.find(entry.edge);
        if (edgeIt == graph->edges.end()) continue;
        const Guid sharedNode = LinkNodeOfEnd(*graph, edgeIt->second, entry.alongEdgeDir ? 1 : 0);
        const TrackNode* nodePtr = FindLinkNode(*graph, sharedNode);
        if (nodePtr == nullptr) continue;
        const TrackNode& node = *nodePtr;
        const std::optional<PointExit> pointExit = ResolvePointExit(node, entry.edge);
        if (!pointExit.has_value() || pointExit->count < 2) continue;
        if (!IsNodeAdjacentToOccupiedBlock(*graph, node, occupiedBlocks)) continue;
        const std::unordered_map<Guid, std::vector<size_t>, GuidHash>::const_iterator blockIt =
            graph->edgeToBlocks.find(next.edge);
        if (blockIt == graph->edgeToBlocks.end()) continue;
        for (size_t blockIndex : blockIt->second) {
            if (occupiedBlocks.contains(graph->blocks[blockIndex].guid)) return next.distance;
        }
    }
    return std::nullopt;
}

}  // namespace rs::world
