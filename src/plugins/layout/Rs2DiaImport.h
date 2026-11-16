#pragma once

#include <string>
#include <vector>

#include "Rs2Types.h"
#include "../../world/alignment/PlatformRoutine.h"

namespace rs::legacy_layout {

// TrainGroup別停車ルーチンの未解決エントリ。TrainIdは編成配置時(app層)に採番される
// ため、取り込み時点ではRs2Addressのまま持ち越し、配置後にPlatformStopInfoへ書き込む。
// 番線ごとに1エントリ(同じDiaInstが駅内の全番線へ配られるため)。
struct PendingPlatformTrainRoutine {
    rs::world::Guid platform = rs::world::NilGuid();
    Rs2Address trainGroup;
    std::vector<rs::world::PlatformRoutineElement> routine;
};

// DiaInst内の1つのTrainGroup別DiaListを変換したもの。番線を跨いで共有されるため、
// 番線に紐付くPendingPlatformTrainRoutineとは別の型にしている。
struct DiaTrainGroupRoutine {
    Rs2Address trainGroup;
    std::vector<rs::world::PlatformRoutineElement> routine;
};

struct ConvertedPlatformRoutines {
    std::vector<rs::world::PlatformRoutineElement> defaultRoutine;
    std::vector<DiaTrainGroupRoutine> trainGroups;
};

// DiaElement 1個をルーチン要素へ変換する。駅のルーチン列の変換と、保存時点で実行中だった
// 1要素の復元が同じ変換を使う。
rs::world::PlatformRoutineElement ConvertDiaElement(const Rs2DiaElement& sourceElement);

ConvertedPlatformRoutines ConvertDiaRoutineToPlatformRoutine(const Rs2DiaInst& diaInst,
                                                              std::vector<std::string>& warnings);

} // namespace rs::legacy_layout
