//
// Rs2RailSchema.h - .rs2 の軌道網ブロック(RailConnector/RailWay/Link)の型付き変換
//
#pragma once

#include "Rs2Types.h"
#include "../schema/parser/Ast.h"

namespace rs::legacy_layout {

// RailConnector{...} を Rs2RailConnector へ変換する。
Rs2RailConnector ParseRailConnector(const rs::legacy_pi::PiBlock& conn);

// RailWay{...} を Rs2RailWay へ変換する。
Rs2RailWay ParseRailWay(const rs::legacy_pi::PiBlock& way);

// Link0/Link1 = <endIdx>, <branchIdx>, <ConnectorAddress>; を読む(Warp・Lineと共用)。
Rs2WayEndLink ParseWayEndLink(const rs::legacy_pi::PiBlock& block, const char* key);

} // namespace rs::legacy_layout
