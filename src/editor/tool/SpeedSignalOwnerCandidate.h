//
// SpeedSignalOwnerCandidate.h - 信号配置の「待ち状態」と、担当閉塞の候補を表す小さな型
//
// UIの操作層(SignalBlockAction)がこの型だけを使うために、巨大な Editor.h から分ける。
//
#pragma once

#include "../../world/alignment/Guid.h"
#include "../../world/alignment/SpeedSignalRegistry.h"

namespace rs::editor {

enum class SignalBlockArmedAction { None, PlaceSpeedSignal };

struct SpeedSignalOwnerCandidate {
    rs::world::Guid block = rs::world::NilGuid();
    rs::world::SpeedSignalPlacementError error = rs::world::SpeedSignalPlacementError::None;
};

} // namespace rs::editor
