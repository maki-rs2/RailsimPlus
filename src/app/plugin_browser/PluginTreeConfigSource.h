//
// PluginTreeConfigSource.h - プラグインのルートから Config2k.txt / Config.txt を選び、
// ディレクトリ分けの木(*PluginTree)を読む
//
// 選び方: ルートを優先順に見て、最初に Config2k.txt があるルートのそれを使う。
// どのルートにも無ければ、最初に Config.txt があるルートのそれ。どちらも無ければ木なし。
// 見るのはルートの直下だけ。走査を始めるときに1回呼ぶ(毎フレームは読まない)。
//
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../../plugins/layout/PluginTreeSchema.h"

namespace rs::app::plugin_browser {

// 読むConfigファイルのパス。どのルートにも無ければnullopt。
std::optional<std::filesystem::path> SelectPluginTreeConfigPath(const std::vector<std::string>& rootsUtf8);

// 選んだConfigを読む。無い・読めないときはnullopt(読めないときは警告を1行出す)。
std::optional<rs::legacy_layout::PluginTreeSet> LoadPluginTreeSet(const std::vector<std::string>& rootsUtf8);

// 選んだConfigのSelectedPlugin(RailSim2で選択中のプラグイン)。キーは種別名、値はプラグイン名。
// 無い・読めないときは空(読めないときは警告を1行出す)。
std::map<std::string, std::string> LoadConfigSelectedPlugins(const std::vector<std::string>& rootsUtf8);

} // namespace rs::app::plugin_browser
