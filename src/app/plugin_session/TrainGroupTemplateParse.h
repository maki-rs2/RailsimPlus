//
// TrainGroupTemplateParse.h - TrainGroupTemplate\*.txt(編成テンプレート)1本の
// パースとインストールルート解決だけを行う純粋な関数。PluginSelectorは触らない。
//
// 適用(ApplyTrainGroupTemplateFile)と、適用せず内容だけ見る表示の両方が使う。
// Reverseのみ車両の指定へ反映し、SwitchOptionは保持するだけで適用しない。
//
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "FormationBuilder.h"

namespace rs::app {

struct TrainGroupTemplateParseResult {
    bool ok = false;                      // パースに成功したか(車両が0両でもtrue)
    std::string formationName;            // テンプレートのファイル名(拡張子なし)
    std::string installRootUtf8;          // Train/<プラグイン名>を探すルート
    std::vector<FormationCarSpec> cars;   // テンプレートの記載順(プラグイン名は未解決)
    std::vector<std::string> warnings;
};

// pluginRootUtf8が空なら、templatePathUtf8の親の親をinstallRootとする。
TrainGroupTemplateParseResult ParseTrainGroupTemplateFile(const std::string& templatePathUtf8,
                                                          const std::string& pluginRootUtf8 = {});

// カードに出す要約。車両の数と、Train/<プラグイン名> のフォルダが無い(見つからない)車両の数。
struct TrainGroupTemplateSummary {
    std::size_t carCount = 0;
    std::size_t missingCount = 0;
};
TrainGroupTemplateSummary SummarizeTrainGroupTemplate(const TrainGroupTemplateParseResult& parsed);

// 車両の指定を、installRootUtf8 配下の Train/<プラグイン名> のフォルダへ解決する(存在は確かめない)。
// TrainPluginが空なら空文字列。適用(BuildFormationFromPlugins)とサムネイルが同じ規約で引く。
std::string ResolveTrainCarFolder(const std::string& installRootUtf8, const FormationCarSpec& spec);

} // namespace rs::app
