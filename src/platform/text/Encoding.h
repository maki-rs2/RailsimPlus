//
// encoding.h - 文字コード変換層
//
// RailSim2 の txt/.x は CP932 (Shift-JIS) で保存されている。
// 入力バイト列は CP932 -> UTF-8 に正規化してから処理する。
// 内部文字列は全て UTF-8。Win32 API 境界でのみ UTF-8 -> ワイド文字列(Windowsは UTF-16、それ以外は UTF-32)に変換する。
//
#pragma once

#include <string>

namespace rs::platform_text {

// CP932 (Shift-JIS) バイト列 -> UTF-8
std::string Cp932ToUtf8(const std::string& sjis);

// UTF-8 -> CP932 (書き出し用。Cp932ToUtf8と対称に用意)
std::string Utf8ToCp932(const std::string& utf8);

// UTF-8 -> ワイド文字列 (Win32 API 呼び出し直前でのみ使用)
std::wstring Utf8ToWide(const std::string& utf8);

// ワイド文字列 -> UTF-8
std::string WideToUtf8(const std::wstring& wide);

// BOM を検出して先頭を進める。UTF-8 BOM があれば true。
bool HasUtf8Bom(const std::string& bytes);

// ファイルを生バイトで読み、CP932 とみなして UTF-8 化して返す。
// UTF-8 BOM 付きならそのまま UTF-8 として扱う（新形式フック）。
// 読み込み失敗時は空文字列を返し ok=false。
std::string ReadFileAsUtf8(const std::wstring& path, bool& ok);

} // namespace rs::platform_text
