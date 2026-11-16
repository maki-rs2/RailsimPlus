//
// PluginDefinitionCache.h - PI定義ファイルのパース結果(PluginDefinition)キャッシュ
//
// ParseFile+BuildPluginDefinitionは定義txtの全文CP932変換+トークナイズ+AST構築を
// 毎回行うが、結果(PluginDefinition)はメッシュ生成と異なり読込直後から不変
// (呼び出し側はplugin.defを読み取り専用でしか参照しない)。
// 同一defFileパスを複数エンティティ(同一フォルダの発車標が多数配置される等)が
// 参照する場合、パースを1回で済ませるためのキャッシュ。ModelManager(.xメッシュ)と
// 同じ「パス+更新時刻キー・コピー返し」方式。
//
#pragma once

#include <filesystem>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "PluginSchema.h"
#include "parser/Diagnostics.h"

namespace rs::legacy_pi {

// ParseFile+BuildPluginDefinition 1回分の結果。okはdoc.nodesが非空だったか
// (=PluginLoader.cppの「空ドキュメント」判定と同じ意味)。
struct ParsedDefinitionResult {
    bool                     ok = false;
    PluginDefinition         def;
    std::vector<Diagnostic>  diagItems; // ParseFile時点のDiagnostics::items()
};

class PluginDefinitionCache {
public:
    // pathの更新時刻が前回キャッシュ時と同じならparseを呼ばずコピーを返す。
    // 変化していれば/未キャッシュならparse()を呼び、結果(ok==trueのみ)を
    // キャッシュしてから返す(失敗はキャッシュしない、ModelManagerと同じ方針)。
    ParsedDefinitionResult Get(const std::wstring& path,
                              const std::function<ParsedDefinitionResult()>& parse);

private:
    struct Entry {
        std::wstring                     path;
        std::filesystem::file_time_type  mtime;
        ParsedDefinitionResult           result;
    };

    // 定義は.xメッシュと違いバイト数が小さいため、バイト予算ではなく件数上限の
    // LRUにする。
    static constexpr size_t kMaxEntries = 512;

    std::mutex                                                    mutex_;
    std::list<Entry>                                              lru_;
    std::unordered_map<std::wstring, std::list<Entry>::iterator>  map_;
};

PluginDefinitionCache& GetPluginDefinitionCache();

} // namespace rs::legacy_pi
