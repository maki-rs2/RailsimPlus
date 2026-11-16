//
// PluginCatalog.h - 走査で見つけたプラグインの一覧(マージ済み、RmlUi非依存)
//
// 走査(PluginCatalogScanner)の結果を
// Mergeで受け取り、同じ相対パスが複数のルートにあれば先のルートを採用する。
// Mergeの順序によらず同じ結果になる。メインスレッド専用。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../plugins/load/PluginHeaderRead.h"
#include "BrowserKindCatalog.h"

namespace rs::app::plugin_browser {

// カタログのEntries()の添字。
using EntryId = std::size_t;

struct PluginCatalogEntry {
    BrowserKind kind = BrowserKind::Rail; // Rail〜Struct(SignalにはしないでhasSignalで表す)
    bool hasSignal = false;
    std::string relativeKey;              // "Rail/foo"(区切りは'/'、綴りは見つかったまま)
    std::string folderUtf8;               // 採用したルートでの絶対パス
    std::size_t rootIndex = 0;            // 採用したルートの添字
    std::vector<std::string> shadowedFoldersUtf8; // 後のルートで見つかった同じ相対パス(整列済み)
    std::string name, author, description;
    std::string iconPathUtf8;             // 空なら無し
    float iconRect[4] = {0.0f, 0.0f, 1.0f, 1.0f}; // IconRect(u, v, 幅, 高さ。0〜1の比率)
};

// ワーカーが1フォルダを調べた結果。
struct PluginScanResult {
    std::uint64_t generation = 0;
    std::size_t rootIndex = 0;
    BrowserKind kind = BrowserKind::Rail;
    std::string relativeKey;
    std::string folderUtf8;
    rs::legacy_plugin::PluginHeaderReadResult header;
};

class PluginCatalog {
public:
    // 結果を1件取り込む。今のGenerationと違うもの、読めなかったもの、ヘッダの種別が
    // 走査したフォルダの種別と食い違うものは捨てる。
    void Merge(const PluginScanResult& result);

    // 空にしてGenerationを1進める(ルートが変わってやり直すとき)。
    void Reset();

    std::uint64_t Generation() const { return generation_; }
    // 追加・変更のたびに増える。表示側はこれを見て絞り込みをやり直す。
    std::uint64_t Revision() const { return revision_; }

    // 追加のみで並べ替えない。添字(EntryId)はGenerationの間は変わらない。
    const std::vector<PluginCatalogEntry>& Entries() const { return entries_; }

    // 採用したフォルダの絶対パスから探す(SamePluginPathで比べる。shadowedは対象外)。
    const PluginCatalogEntry* Find(const std::string& folderUtf8) const;

    // 大文字小文字を区別せずrelativeKeyから探し、EntryIdを返す。
    std::optional<std::size_t> FindByRelativeKey(const std::string& relativeKey) const;

private:
    std::vector<PluginCatalogEntry> entries_;
    std::unordered_map<std::string, std::size_t> indexByKey_;   // ASCII小文字化したrelativeKey -> EntryId
    // フォルダ名(ASCII小文字)->EntryIdの候補。SamePluginPathはファイルシステムに触って重いため、
    // 同名のフォルダだけに絞ってから比べる。
    std::unordered_multimap<std::string, std::size_t> indexByLeaf_;
    std::uint64_t generation_ = 0;
    std::uint64_t revision_ = 0;
};

} // namespace rs::app::plugin_browser
