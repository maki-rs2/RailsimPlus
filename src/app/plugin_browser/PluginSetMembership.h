//
// PluginSetMembership.h - カタログの各エントリが、レールの敷設プラグインセットに入っているかの覚え書き
//
// セットとの比較はSamePluginPath(ファイルシステムに触って重い)なので、カードごとに毎フレーム
// 呼ばない。セットのfolder列が変わったときに覚えた結果を捨て、聞かれたエントリだけ計算し直す。
// エントリのフォルダが(別のルートに勝たれて)変わったときも、そのエントリだけ計算し直す。
// メインスレッド専用。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "PluginBrowserState.h"
#include "PluginCatalog.h"

namespace rs::app::plugin_browser {

class PluginSetMembership {
public:
    // セットの今のfolder列を渡す。前回と変わっていれば覚えた結果を捨てる。
    void SetFolders(const std::vector<std::string>& setFolders);

    // entryがセットに入っているか。idはentryのEntryId(覚え書きのキー)。
    bool Contains(EntryId id, const PluginCatalogEntry& entry);

    // セットのfolder列が変わるたびに増える版(絞り込みをやり直す合図)。
    std::uint64_t Revision() const { return revision_; }

private:
    struct Remembered {
        std::string folderUtf8; // 計算したときのエントリのフォルダ
        bool inSet = false;
    };

    std::uint64_t revision_ = 0;
    std::vector<std::string> setFolders_;
    std::unordered_map<EntryId, Remembered> cache_;
};

} // namespace rs::app::plugin_browser
