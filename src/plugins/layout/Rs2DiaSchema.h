#pragma once

#include "Rs2Types.h"
#include "../schema/parser/Ast.h"

namespace rs::legacy_layout {

// DiaElement{ Action; TimeType; Time = h, m, s; Offset; } 1ブロック分を読む。
// DiaInst 配下と TrainGroup の走行状態の双方から同じ構文で使う。
Rs2DiaElement ParseDiaElementBlock(const rs::legacy_pi::PiBlock& block);

// RailConnector ブロック直下の PointInst を読む。無ければ present=false。
Rs2PointInst ParsePointInst(const rs::legacy_pi::PiBlock& conn);

// Station ブロック直下の DiaInst を読む。無ければ present=false。
Rs2DiaInst ParseDiaInst(const rs::legacy_pi::PiBlock& station);

} // namespace rs::legacy_layout
