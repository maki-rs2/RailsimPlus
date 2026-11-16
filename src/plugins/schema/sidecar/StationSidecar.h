//
// StationSidecar.h - 駅向けプラグイン拡張JSONの読込結果
//
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../parser/Diagnostics.h"
#include "PluginSidecarCommon.h"

namespace rs::legacy_pi::sidecar {

constexpr const char* kStationSidecarFileName = "station.json";

struct StationSidecar {
    bool loaded = false;
    bool hasSignal = false;
    SignalTable signal;
    SwitchMappingTable table;
    std::vector<Diagnostic> diagnostics;
};

// pluginFolder直下のstation.jsonを読む。ファイルが無い場合は診断を残さない。
StationSidecar LoadStationSidecar(const std::wstring& pluginFolder);

// 正規化後の段(段0から順)が元signalTableのどの要素に由来するかを返す。
// BuildAspectsFromSignalTable/BuildAspectAssignmentsFromSignalTableと同じ段順序で、
// 信号に保存する段のsidecarIndexはこの値。signalTableが空なら空。
std::vector<size_t> BuildAspectSourceIndices(const SignalTable& signalTable);

// 消灯の段(速度が負の最初の要素)の元signalTableでの要素番号。段としては数えない。無ければnullopt。
std::optional<size_t> FindDarkStageSourceIndex(const SignalTable& signalTable);

// signalTableから信号エンティティへseedするaspects(制限速度[km/h]の配列、先頭0・厳密昇順)を組み立てる。
// 段0は速度0の要素を採用し、無ければ配列先頭の要素をフォールバックに採用する
// (いずれの場合もseedするaspects[0]は0固定)。
// 段0に採用しなかった要素は速度昇順に並べる。速度が負の要素は段として扱わず除外し、
// 速度が重複する要素は最初の1件のみ採用する。signalTableが空ならaspectsも空を返す。
std::vector<int> BuildAspectsFromSignalTable(const SignalTable& signalTable);

// BuildAspectsFromSignalTableと同じ段順序で、各段のスイッチ割当を返す
// (呼び出し側はAspectIndexOf等で得た段番号をこの配列に対してもそのまま引ける)。
std::vector<SwitchAssignment> BuildAspectAssignmentsFromSignalTable(const SignalTable& signalTable);

// 運行情報KV(type/destination/lineName/trainNumber/dutyNumber)をstation.jsonの対応表で解決し、
// 書き込むスイッチ割り当てを組み立てる。呼び出し側のTrainRunInfoが持つ
// lastDepartureStation/stopStationsは対象外なので引数に取らない
// (plugins/schema層をworld層のTrainRunInfo型に依存させないため、フィールドを個別の文字列で受ける)。
SwitchAssignment BuildRunInfoSwitchAssignment(
    const SwitchMappingTable& table, const std::string& type, const std::string& destination,
    const std::string& lineName, const std::string& trainNumber, const std::string& dutyNumber);

} // namespace rs::legacy_pi::sidecar
