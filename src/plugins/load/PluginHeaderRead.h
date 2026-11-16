//
// PluginHeaderRead.h - プラグインフォルダのヘッダ(PluginHeaderブロック)だけを読む
//
// ブラウザの一覧づくり用。モデルやテクスチャは読まず、定義ファイルのうちPluginHeader
// ブロックの終わりまでだけを構文解析して、PluginHeaderの6項目と、アイコンの場所、
// 信号の有無を返す(大きな駅・建造物の定義は数MBあり、全体を解析すると1件で数秒かかるため)。
// PluginDefinitionCacheは通さない(全件走査でキャッシュを押し出さないため)。
// グローバルな状態を書かないので、ワーカースレッドから呼んでよい。
//
#pragma once

#include <string>

#include "../schema/PluginSchema.h"

namespace rs::legacy_plugin {

struct PluginHeaderReadResult {
    bool ok = false;           // 定義ファイルを読めて、PluginHeaderがあった
    bool noDefinition = false; // *2.txt / *2k.txt が無い(共有リソースのフォルダなど)
    rs::legacy_pi::PluginHeader header;
    std::string iconPathUtf8;  // IconTextureを定義ファイルのフォルダ基準で絶対パスにしたもの。ファイルが無ければ空
    bool hasSignal = false;    // 種別がStationで、station.jsonにsignalキーがある
};

PluginHeaderReadResult ReadPluginHeader(const std::wstring& folder);

// 定義ファイルの本文(UTF-8)のうち、最上位のPluginHeaderブロックの閉じ括弧の直後までの
// 長さを返す。文字列(\"のエスケープを含む)とコメント(//、/* */)の中の括弧や語は数えない。
// 見つからなければstd::string::nposを返す(呼び出し側は全体を解析する)。
std::size_t FindPluginHeaderEnd(const std::string& utf8);

} // namespace rs::legacy_plugin
