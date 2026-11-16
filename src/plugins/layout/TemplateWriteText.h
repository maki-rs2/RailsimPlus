//
// TemplateWriteText.h - テンプレート書出し(RailwayPluginSet / TrainGroupTemplate)の共通部
//
// 書出し側は UTF-8 の本文を作り、CP932への変換は保存側(TemplateSaver)が行う。
// 文字列の値が書けるかの検査(引用符・制御文字・CP932で表せない文字)をここに集め、
// 2種類のWriterで同じ判定を使う。
//
#pragma once

#include <string>

namespace rs::legacy_layout {

// 書出し結果。ok=falseなら本文は空で、badFieldに書けなかった項目名(例 "RailPlugin")を入れる。
struct TemplateWriteResult {
    bool        ok = false;
    std::string text;      // UTF-8、CRLF改行
    std::string badField;
};

// 文字列の値として書けるか。`"`・制御文字(改行を含む)・CP932で表せない文字を含むと偽。
// 文字の置換はしない。
bool IsWritableTemplateString(const std::string& utf8);

// 値を `"..."` で囲む。IsWritableTemplateStringを満たす前提。
std::string QuoteTemplateString(const std::string& utf8);

}  // namespace rs::legacy_layout
