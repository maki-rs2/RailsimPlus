//
// PluginTreeSchema.h - Config2k.txt / Config.txt の *PluginTree ブロック
// (ユーザーが作ったプラグインのディレクトリ分け)を型付きの木へ変換する
//
// 種別の絞り込みはこの層では行わず、File 行に書かれた種別を見つかったまま保持する。
//
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../schema/parser/Ast.h"

namespace rs::legacy_layout {

// File = 種別, "名前"; 1行ぶん。kind は1つ目の値(Rail, Train, TrainGroupTemplate など)。
struct PluginTreeFile {
    std::string kind;
    std::string name;
};

// Directory "名前"{ ... } 1つぶん。最上位(*PluginTree ブロック自体)は name が空。
// dirs と files はそれぞれファイル中の並び順を保つ。
struct PluginTreeDir {
    std::string                name;
    std::vector<PluginTreeDir> dirs;
    std::vector<PluginTreeFile> files;
};

struct PluginTreeSet {
    // キーは「PluginTree」を除いた接頭辞(Rail, Train, Station など)。ファイルに出た順。
    std::vector<std::pair<std::string, PluginTreeDir>> trees;

    // 大文字小文字を区別せずに探す。無ければ nullptr。
    const PluginTreeDir* Find(std::string_view kind) const;
};

// トップレベルで名前が「PluginTree」で終わるブロックを木として読む。
// 不正な File 行は warnings に1行足して読み飛ばす。
PluginTreeSet ParsePluginTrees(const rs::legacy_pi::PiDocument& doc,
                               std::vector<std::string>& warnings);

// ConfigMode/SelectedPlugin の「種別 = \"名前\";」を、ファイル中の並びのまま返す(種別はRail, Trainなど)。
// 文字列でない値は読み飛ばす。ブロックが無ければ空。
std::vector<std::pair<std::string, std::string>> ParseSelectedPlugins(const rs::legacy_pi::PiDocument& doc);

// Config ファイル1本(CP932)を読む。開けない・パースエラーのときは std::nullopt。
std::optional<PluginTreeSet> ParsePluginTreeFile(const std::filesystem::path& path,
                                                 std::vector<std::string>& warnings);

// Config ファイル1本から SelectedPlugin だけを読む。開けない・パースエラーのときは std::nullopt。
std::optional<std::vector<std::pair<std::string, std::string>>> ParseSelectedPluginFile(
    const std::filesystem::path& path);

} // namespace rs::legacy_layout
