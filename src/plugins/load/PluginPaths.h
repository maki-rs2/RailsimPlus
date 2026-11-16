//
// PluginPaths.h - PI プラグイン読み込みのパス解決/ファイル探索ヘルパ
//
// プラグインフォルダ内の相対パス（..\ 混在・プラグイン跨ぎ）解決と、定義ファイル
// (*2k.txt / *2.txt) 候補の列挙を担う。文字コードは Win32 境界の wide 前提で、
// UTF-8 との相互変換は encoding.h（呼び出し側）で行う。
//
#pragma once

#include <string>
#include <vector>

namespace rs::legacy_plugin {

// キャッシュと比較に使う絶対パス。区切り・末尾の区切り・大文字小文字を揃える。
std::string NormalizePluginFolderKey(const std::string& folderUtf8);

// 大文字小文字を区別しない文字列比較(0で一致)。
int CompareNoCase(const wchar_t* a, const wchar_t* b);

// path が存在し、かつディレクトリでない（＝ファイル）なら true。
bool FileExists(const std::wstring& path);

// 区切りをこのOSの区切り(Windowsは'\\'、それ以外は'/')へそろえたコピーを返す。
std::wstring NormalizeSlashes(std::wstring s);

// baseDir を基準に相対パス relUtf8 を解決（..\ や ../ 混在に対応）。
// 見つかった正規化パスを返す。存在確認はしない。絶対パスはそのまま通す(Windowsはドライブ/UNC、それ以外は'/'始まり)。
std::wstring ResolvePath(const std::wstring& baseDir, const std::string& relUtf8);

// 解決済みのベース画像パス(wide)の隣にある「<画像名><suffix><拡張子>」を探す。あればUTF-8のパスを返し、
// 無ければ(拡張子が無い場合も)空文字列を返す。法線(_n)・ORM(_orm)の自動探索に使う。
std::string FindPairedTexture(const std::wstring& resolvedBase, const wchar_t* suffix);

// path のディレクトリ部分（末尾の区切り手前まで）を返す。区切りが無ければ空。
std::wstring DirOf(const std::wstring& path);

// フォルダ内の定義ファイル候補を列挙する。並びは、親フォルダ名と同じ種別の *2k.txt →
// 同じ種別の *2.txt → それ以外の *2k.txt → それ以外の *2.txt(Train/xxx に Struct2.txt と
// Train2.txt があれば Train2.txt が先)。親フォルダが種別名でなければ *2k.txt → *2.txt。
std::vector<std::wstring> FindDefinitionFiles(const std::wstring& folder);

// フォルダ内の定義ファイルを1つ選ぶ。親フォルダ名と同じ種別の定義があればその中から、
// 無ければ残りから選ぶ。preferKBuild=true なら *2k.txt を優先し、false なら標準版(*2.txt)を
// 優先する(片方しか無ければそれを返す)。
// 定義ファイルが1つも無ければ空文字列を返す。
std::wstring PickDefinitionFile(const std::wstring& folder, bool preferKBuild);

} // namespace rs::legacy_plugin
