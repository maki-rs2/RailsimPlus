//
// TemplateSaver.h - 敷設テンプレート(RailwayPluginSet)・編成テンプレート(TrainGroupTemplate)の保存
//
// プラグインルート直下の種類別フォルダ(<root>/RailwayPluginSet/<名前>.txt など)へ、
// K版互換の本文をCP932で原子的に置き換えて書く。読込側(ApplyRailwayPluginSetFile /
// ParseTrainGroupTemplateFile)はファイルの祖父ディレクトリをルートとするので、同じ配置になる。
// 同名の上書きは確認が要るため、Save*は無条件に置き換え、確認は呼び出し側が
// TemplateFileExistsで先に行う。
//
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "../../plugins/layout/RailwayPluginSetSchema.h"
#include "../../plugins/layout/SceneryTemplateSchema.h"
#include "../../plugins/layout/TrainGroupTemplateSchema.h"

namespace rs::app {

// 保存先のフォルダ名はTemplateSaver.cppの表で持つ(列挙の並びと合わせる)。
// 駅・施設のテンプレートは、名前の走査(ブラウザの一覧)だけに使う。
enum class TemplateKind { RailwayPluginSet, TrainGroupTemplate, StationBuildTemplate, StructBuildTemplate };

struct TemplateSaveResult {
    bool ok = false;
    std::filesystem::path path;  // 書き込み先(失敗時も、導出できていれば入る)
    std::string error;           // 失敗時の英語の診断文言
    // 保存を中止した原因の項目名(CP932で表せない文字などを含む項目)。値の問題でなければ空。
    std::string badField;
};

// 保存先のファイルパス。名前が空・ファイル名に使えない文字を含む場合は空のパスを返す。
std::filesystem::path TemplateSavePath(const std::string& pluginRootUtf8, TemplateKind kind,
                                       const std::string& nameUtf8);

// 保存先に同名のファイルが既にあるか。名前が不正なら偽。
bool TemplateFileExists(const std::string& pluginRootUtf8, TemplateKind kind,
                        const std::string& nameUtf8);

// 保存先フォルダにある `.txt` の、拡張子を除いた名前(ファイル名順)。フォルダが無い・空なら空。
// 同名の判定はTemplateFileExistsと同じファイルシステムの規則(Windowsでは大文字小文字を区別しない)に従う。
std::vector<std::string> ListTemplateNames(const std::string& pluginRootUtf8, TemplateKind kind);

// 追加のプラグイン(RailSim IIでは使われない2つ目以降)を持つセットか。保存前の警告の判定に使う。
bool RailwayPluginSetNeedsExtraWarning(const rs::legacy_layout::RailwayPluginSetDef& def);

TemplateSaveResult SaveRailwayPluginSetTemplate(const std::string& pluginRootUtf8,
                                                const std::string& nameUtf8,
                                                const rs::legacy_layout::RailwayPluginSetDef& def);

TemplateSaveResult SaveTrainGroupTemplate(const std::string& pluginRootUtf8,
                                          const std::string& nameUtf8,
                                          const rs::legacy_layout::TrainGroupTemplateDef& def);

// 駅・施設のテンプレート。保存先のフォルダは def.kind で決まる。
TemplateSaveResult SaveSceneryTemplate(const std::string& pluginRootUtf8,
                                       const std::string& nameUtf8,
                                       const rs::legacy_layout::SceneryTemplateDef& def);

}  // namespace rs::app
