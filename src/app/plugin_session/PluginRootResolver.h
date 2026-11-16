#pragma once

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace rs::app {

std::string RelativizeToPluginRoot(const std::vector<std::string>& rootsUtf8,
                                   const std::string& absoluteUtf8);
// rootsUtf8は優先度順（先勝ち）。storedUtf8と同じ相対パスが複数のrootに存在する場合、
// 先頭に近いrootを採用し、後続rootにも同じ相対パスが存在することをwarnedConflictsUtf8で
// 重複排除しながら1回だけ警告する（仕様確定: 優先順位=登録順、保存不要、警告のみで足りる）。
std::string ResolveAgainstPluginRoots(const std::vector<std::string>& rootsUtf8,
                                      const std::string& storedUtf8,
                                      std::set<std::string>& warnedConflictsUtf8);
// プラグイン参照を解決する候補ルートを優先度順に組み立てる。derivedRootUtf8 は
// レイアウトファイル自身から導出したルート（.rs2 のみ。.rsl では空を渡す）。
// 空文字列の候補と重複は取り除く。
std::vector<std::string> BuildPluginRootCandidates(const std::string& derivedRootUtf8,
                                                   const std::vector<std::string>& configuredRootsUtf8,
                                                   const std::string& executableDirectoryUtf8);

// The plugin, folders, and the legacy layout key are reserved for plugin references in layout JSON.
void RelativizeLayoutPluginReferences(nlohmann::json& document,
                                     const std::vector<std::string>& rootsUtf8);
// 解決したプラグイン参照の綴りを、すでに登録されている同じプラグインの綴りへ揃える関数。
// 解決結果は区切りが常に"/"になるが、登録済みの綴りはOSの区切り("\\")のこともある。
// 綴りが違うとedgeのプラグイン区間の完全一致比較(FolderRanges等)が外れてレールが描かれないため、
// 読込の時点で揃える。登録が無ければ渡された綴りをそのまま返す。
using PluginPathCanonicalizer = std::function<std::string(const std::string&)>;
// canonicalizeは空でもよい(その場合は解決結果をそのまま使う)。
void ResolveLayoutPluginReferences(nlohmann::json& document,
                                   const std::vector<std::string>& rootsUtf8,
                                   const PluginPathCanonicalizer& canonicalize = {});

}  // namespace rs::app
