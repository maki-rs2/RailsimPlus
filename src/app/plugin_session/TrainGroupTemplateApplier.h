//
// TrainGroupTemplateApplier.h - TrainGroupTemplate\*.txt(編成テンプレート)1本を
// パースし、各TrainTemplateをPluginSelectorの編成リスト(TrainEntries)へ追加する
// (app層グルー、RailwayPluginSetApplier.hと同じ位置づけ)。
//
// Train は単体配置不可で必ず編成リストへ追加する既存方針(PluginSelector.h)に
// 合わせ、このApplierが直接TrainEntriesへ積む(RailwayPluginSetApplierと異なり
// edge選択/Undo分岐が無く重複追加も許容されるため、UI側で追加ロジックを分岐する
// 必要が無い)。Reverseのみ反映し、SwitchOptionは
// TrainGroupTemplateDef側で保持するのみ(適用しない)。
//
#pragma once

#include <string>
#include <vector>

namespace rs::app {

class PluginSelector;

// ApplyTrainGroupTemplateFile の結果(診断表示用)。
struct TrainGroupTemplateApplyResult {
    bool ok = false;
    size_t addedCount = 0;
    std::vector<std::string> warnings;
};

// TrainGroupTemplate\*.txt(templatePathUtf8)をパースし、各TrainTemplateを
// installRoot(=templatePathUtf8の親の親)配下のTrain/<プラグイン名>へ解決した上で
// selector.AddPath で編成リストへ追加する(Reverse=yesならAddPath後にreversedを
// 立てる)。解決/読込に失敗した車両はスキップしwarningsへ記録する。
TrainGroupTemplateApplyResult ApplyTrainGroupTemplateFile(const std::string& templatePathUtf8,
                                                          PluginSelector& selector,
                                                          const std::string& pluginRootUtf8 = {});

} // namespace rs::app
