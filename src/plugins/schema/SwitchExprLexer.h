#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rs::legacy_pi::switch_expr_detail {

// ラベル文字列専用の軽量トークン。Lexer.h の TokenKind とは独立（ラベルは
// 既に「空白区切りで連結された不透明文字列」であり、Parser.cpp 側の元
// トークン境界は失われているため、ここで再トークナイズする）。
enum class ExprTokKind {
    Number, String, Op, LParen, RParen, Question, Colon, End
};

struct ExprTok {
    ExprTokKind kind = ExprTokKind::End;
    int32_t     number = 0;
    std::string text; // String の中身、または Op の記号列
};

// 字句解析。数値は10進整数（先頭に単項符号が付くことは字句レベルでは扱わず、
// 単項演算子として構文解析側で処理する）。文字列は "..." （中に日本語・空白
// を許容。エスケープは考慮不要な仕様）。
class ExprLexer {
public:
    explicit ExprLexer(const std::string& s) : s_(s) {}

    bool Tokenize(std::vector<ExprTok>& out, std::string& err);

private:
    const std::string& s_;
};

} // namespace rs::legacy_pi::switch_expr_detail
