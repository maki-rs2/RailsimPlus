//
// PluginTools.h - プラグイン/.x/PI読込の診断CLIツール（ウィンドウ/デバイス不要）
//
#pragma once

#include <string>
#include <vector>

namespace rs::app {

// ヘッドレス: .x を読み統計を出力
int RunTestX(const std::string& utf8Path);

// ヘッドレス: txt を AST 化
int RunTestPi(const std::string& utf8Path);

// 種別フォルダ配下の各プラグインサブフォルダを列挙
std::vector<std::wstring> ListSubdirs(const std::wstring& dir);

// ヘッドレス: 1プラグインフォルダを読み部品/PBR情報をダンプ
int RunTestPlugin(const std::string& folderUtf8, const std::string& defFile);

} // namespace rs::app
