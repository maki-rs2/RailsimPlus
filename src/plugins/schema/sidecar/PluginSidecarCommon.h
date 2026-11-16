//
// PluginSidecarCommon.h - プラグイン拡張JSONの公開データ構造
//
// PluginType向けサイドカーは、ファイル名定数1個・専用struct1個・
// 種別固有キーのパース関数1個を持ち、この共通テーブルの読込を呼び出す。
//
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rs::legacy_pi::sidecar {

using SwitchAssignment = std::map<std::string, int>;

struct SwitchValueTable {
    std::map<std::string, SwitchAssignment> entries;
};

struct DigitSwitchDigit {
    std::string switchLabel;
    std::map<std::string, int> charToCase;
};

using DigitSwitchTable = std::vector<DigitSwitchDigit>;

struct SignalStage {
    double speedKmh = 0.0;
    SwitchAssignment assignment;
};

using SignalTable = std::vector<SignalStage>;

struct SwitchMappingTable {
    std::optional<SwitchValueTable> type;
    std::optional<SwitchValueTable> destination;
    std::optional<SwitchValueTable> lineName;
    std::optional<SwitchValueTable> lastDepartureStation;
    std::optional<DigitSwitchTable> trainNumber;
    std::optional<DigitSwitchTable> dutyNumber;
};

} // namespace rs::legacy_pi::sidecar
