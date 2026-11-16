//
// EnvPluginLoad.h - Envプラグインのフォルダ(Env/<名前>/)から定義(Env2k.txt / Env2.txt)を読む
//
// 定義ファイルの選び方はプラグイン共通の規則(PickDefinitionFile、*2k.txt優先)に従う。
// 読むのは定義の値だけで、Landscapeのモデルやキューブのテクスチャは読まない。
// グローバルな状態を書かない(ログも出さない。呼び出し側が結果を見て出す)。
//
#pragma once

#include <string>
#include <vector>

#include "../schema/EnvSchema.h"
#include "../schema/parser/Diagnostics.h"

namespace rs::legacy_plugin {

struct EnvPluginLoadResult {
    bool ok = false;           // 定義ファイルを読めて、1ブロック以上あった(構文エラーがあっても読めた分は使う)
    bool noDefinition = false; // 定義ファイルが無い(他のEnvから参照されるだけの共有素材フォルダなど)。失敗ではない
    std::wstring defPath;      // 読んだ定義ファイル
    rs::legacy_pi::EnvDefinition definition;
    std::vector<rs::legacy_pi::Diagnostic> diagnostics; // 構文解析・値の読み取りの診断
};

EnvPluginLoadResult LoadEnvPlugin(const std::wstring& folder, bool preferKBuild = true);

// Envのフォルダ基準で、定義に書かれたテクスチャの相対パス(../Default/x.dds のような親フォルダ参照も可)を
// 解決し、正規化したUTF-8のパス(区切りは/)を返す。区切りは/と\のどちらも受ける。相対パスが空なら空文字列。
// 先頭の区切りは付けても無視する(フォルダ基準のまま)。存在確認はしない(読み込む側が行う)。
std::string ResolveEnvTexturePath(const std::string& envFolderUtf8, const std::string& relativePathUtf8);

} // namespace rs::legacy_plugin
