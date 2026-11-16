//
// EditorCenterlineSplit.cpp - EditorCenterlineSplit.h の実装
//
#include "EditorCenterlineSplit.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "../../core/Epsilon.h"
#include "../../world/alignment/EdgeSplitMerge.h"
#include "Command.h"
#include "Editor.h"
#include "EdgeSplitMergeCommand.h"

namespace rs::editor {

using rs::world::Guid;
using rs::world::NilGuid;
using rs::world::TrackEdge;

Guid FindOwningCenterlineGroup(const rs::world::TrackGraph& graph, const Guid& edgeGuid) {
    const auto it = graph.edges.find(edgeGuid);
    if (it == graph.edges.end()) return NilGuid();
    if (it->second.centerlineOnly) return edgeGuid;
    for (const std::pair<const Guid, TrackEdge>& kv : graph.edges) {
        if (!kv.second.centerlineOnly) continue;
        const std::vector<Guid>& members = kv.second.trackEdges;
        if (std::find(members.begin(), members.end(), edgeGuid) != members.end())
            return kv.first;
    }
    return NilGuid();
}

bool SplitCenterlineGroupAt(Editor& ed, const Guid& centerlineGuid, double sCenter) {
    const auto centerIt = ed.graph.edges.find(centerlineGuid);
    if (centerIt == ed.graph.edges.end()) return false;
    const TrackEdge centerBefore = centerIt->second; // コピー(下のExecuteCommandでイテレータが無効化されうる)
    if (rs::world::CheckEdgeSplit(ed.graph, centerBefore, sCenter) !=
        rs::world::EdgeSplitRefusal::None)
        return false;
    if (centerBefore.spline.empty()) return false;
    const double lenCenter = static_cast<double>(centerBefore.spline.back().s);
    if (lenCenter <= rs::core::kDegenerateVectorEps) return false;

    // 各memberの対応するs(中心線との弧長比で比例配分)を求め、実行前に全member+
    // 中心線が分割可能かを検証する(1つでも不可なら全体を拒否、部分適用を避ける)。
    struct MemberSplitPlan {
        Guid memberGuid;
        double memberS;
    };
    std::vector<MemberSplitPlan> plans;
    plans.reserve(centerBefore.trackEdges.size());
    for (const Guid& memberGuid : centerBefore.trackEdges) {
        const auto mIt = ed.graph.edges.find(memberGuid);
        if (mIt == ed.graph.edges.end()) return false;
        const TrackEdge& member = mIt->second;
        if (member.spline.empty()) return false;
        const double lenMember = static_cast<double>(member.spline.back().s);
        const double memberS = sCenter * (lenMember / lenCenter);
        if (rs::world::CheckEdgeSplit(ed.graph, member, memberS) !=
            rs::world::EdgeSplitRefusal::None)
            return false;
        plans.push_back({memberGuid, memberS});
    }

    // 全件検証済み: 中心線+全memberを1つのCompositeCommandへまとめて実行する。
    std::unique_ptr<CompositeCommand> composite = std::make_unique<CompositeCommand>();
    std::unique_ptr<SplitEdgeCommand> centerSplitCmd =
        std::make_unique<SplitEdgeCommand>(centerlineGuid, sCenter, ed.manualOverlay);
    const Guid centerSecondGuid = centerSplitCmd->NewEdgeGuid();
    composite->Add(std::move(centerSplitCmd));

    std::vector<Guid> secondHalfMembers;
    secondHalfMembers.reserve(plans.size());
    for (const MemberSplitPlan& plan : plans) {
        std::unique_ptr<SplitEdgeCommand> memberSplitCmd =
            std::make_unique<SplitEdgeCommand>(plan.memberGuid, plan.memberS, ed.manualOverlay);
        secondHalfMembers.push_back(memberSplitCmd->NewEdgeGuid());
        composite->Add(std::move(memberSplitCmd));
    }

    // 中心線の後半(新guid)はBuildEdgeSplitがtrackEdgesに触れないため、分割元の
    // 古いmember guid列をそのままコピーして持ってしまう。後半memberの新guid列へ
    // 明示的に差し替える(前半は元guidのまま有効なmember参照を保つため変更不要)。
    EdgePropertyDelta secondHalfDelta;
    secondHalfDelta.trackEdges = secondHalfMembers;
    composite->Add(std::make_unique<SetEdgePropertyCommand>(centerSecondGuid, secondHalfDelta));

    if (!ed.ExecuteCommand(std::move(composite))) return false;
    ed.SelectEdge(centerlineGuid);
    ed.ResetSelectedSectionIndices();

    return true;
}

} // namespace rs::editor
