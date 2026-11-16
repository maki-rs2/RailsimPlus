//
// MissingPluginCatalog.h - レイアウトが参照しているのに見つからないプラグインの集計
//
// 「参照の列挙」(CollectPluginReferences。訪問はPluginReferenceWalk.h)と
// 「存在判定と集計」(AggregateMissingPlugins)を分けている。集計は純粋関数で、存在判定を関数で受けるため、ファイルシステムに触れずに試せる。
//
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "PluginReferenceWalk.h"

namespace rs::app {

// 見つからないプラグイン1行ぶん。キーは (PluginFolderKey(folderUtf8), site)。
// 同じフォルダが別の場所からも参照されていれば、siteごとに別の行になる。
// folderUtf8は、そのキーで最初に見つかった表記。
struct MissingPluginEntry {
    std::string folderUtf8;
    PluginReferenceSite site = PluginReferenceSite::SceneryEntity;
    size_t usageCount = 0;  // その場所からの参照の数(個体数)
};

// folderUtf8のプラグインフォルダが存在するか。
using PluginExistsFn = std::function<bool(const std::string& folderUtf8)>;

// 実際のファイルシステムでフォルダの有無を調べる(PluginExistsFnの標準の実装)。
bool PluginFolderExists(const std::string& folderUtf8);

// 現在のドキュメントの全参照を集める。保存処理(SaveRslLayout)と同じ範囲を見る。
// ・全Scene: アクティブなSceneはライブ状態(editor・sceneRegistry・importedWireSpans)、
//   ほかのSceneはsessionのキャッシュを使う。sessionが空ならライブ状態だけを見る。
// ・一時的な配置物は含めない。
// ・環境プラグインは、解決できたときはそのフォルダ、できなかったときはpluginRootsから組んだ
//   `<ルート>/Env/<名前>`(pluginRootsが無ければ名前)を入れる。
// importedWireSpansがnullptrなら、架線のライブ配列を持たない呼び出し側として、
// アクティブSceneもキャッシュ済みの値を使う。
std::vector<PluginReference> CollectPluginReferences(
    const rs::editor::Editor& editor, const rs::world::SceneRegistry& sceneRegistry,
    const LayoutSession& session, const PluginSelector& selector,
    const std::vector<ImportedWireSpan>* importedWireSpans = nullptr,
    const std::vector<std::string>* pluginRoots = nullptr);

// 参照のうち存在しないものを (PluginFolderKey(folderUtf8), site) ごとに数える。
// 空文字の参照は未指定の意味なので対象外。結果はキーの昇順、次にsiteの順。
std::vector<MissingPluginEntry> AggregateMissingPlugins(const std::vector<PluginReference>& references,
                                                        const PluginExistsFn& exists);

}  // namespace rs::app
