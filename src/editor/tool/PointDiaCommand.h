//
// PointDiaCommand.h - 分岐(TrackNode)のダイヤのデータ(名前・所属駅・ルーチン・条件)を差し替えるコマンド
//
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Command.h"
#include "../../world/alignment/PointRoutine.h"

namespace rs::editor {

// 差し替える項目だけを持つ。層ごとの変更ではなく、その種類の一覧をまるごと渡す(Undo 1回で戻る)。
struct PointDiaDelta {
    std::optional<std::string> name;
    std::optional<rs::world::Guid> stationId;
    std::optional<std::vector<rs::world::PointRoutineElement>> routine;
    std::optional<std::vector<rs::world::PointFormationRoutine>> formationRoutines;
    std::optional<std::vector<rs::world::PointTagRoutine>> tagRoutines;
    std::optional<std::vector<rs::world::PointCondition>> conditions;
    std::optional<std::vector<rs::world::PointFormationCondition>> formationConditions;
    std::optional<std::vector<rs::world::PointTagCondition>> tagConditions;
};

class SetPointDiaCommand : public ICommand {
public:
    SetPointDiaCommand(rs::world::Guid nodeId, PointDiaDelta delta);
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
    bool Applied() const override { return hadOld_; }

private:
    rs::world::Guid nodeId_;
    PointDiaDelta delta_;
    PointDiaDelta oldValues_;
    bool hadOld_ = false;
};

} // namespace rs::editor
