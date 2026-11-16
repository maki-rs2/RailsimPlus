//
// Lexer.h - PI/.rs2 共通構文トークナイザ
//
#pragma once

#include <string>
#include <vector>

#include "Diagnostics.h"

namespace rs::legacy_pi {

enum class TokenKind {
    Identifier,
    String,
    Number,
    HexAddress, // 16桁 hex（保存時アドレス。文字列のまま保持）
    HexColor,   // #RRGGBB / #AARRGGBB（Diffuse等の色リテラル。'#'を除いた桁を保持）
    LBrace,     // {
    RBrace,     // }
    LParen,     // (
    RParen,     // )
    Equals,     // =
    Semicolon,  // ;
    Comma,      // ,
    Colon,      // :
    Operator,   // 条件式の演算子 < > ! | & 等（If/ApplySwitch 条件用。text に生記号）
    End
};

struct Token {
    TokenKind   kind = TokenKind::End;
    std::string text;       // Identifier/String中身/HexAddress の生値、または数値の元表記
    double      number = 0; // Number のとき
    int         line = 0;   // 1-based
    int         column = 0; // 1-based
};

// UTF-8 文字列（CP932->UTF-8 変換済み）をトークン列に分解する。
std::vector<Token> Tokenize(const std::string& utf8, Diagnostics& diag);

} // namespace rs::legacy_pi
