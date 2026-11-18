#include "Lexer.h"

#include <cctype>
#include <cstdlib>
#include <string_view>

namespace rs::legacy_pi {

namespace {

struct Cursor {
    const std::string& s;
    size_t             pos = 0;
    int                line = 1;
    int                col = 1;

    explicit Cursor(const std::string& src) : s(src) {}

    bool Eof() const { return pos >= s.size(); }
    char Peek(size_t off = 0) const {
        return pos + off < s.size() ? s[pos + off] : '\0';
    }
    char Get() {
        char c = s[pos++];
        if (c == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
        return c;
    }
    // 改行を含まない n 文字を一度に進める(識別子・16進など、改行にならない区間用)。
    void SkipNoNewline(size_t n) {
        pos += n;
        col += static_cast<int>(n);
    }
};

bool IsIdentStart(unsigned char c) { return std::isalpha(c) || c == '_'; }
bool IsIdentChar(unsigned char c) { return std::isalnum(c) || c == '_'; }
bool IsHexDigit(unsigned char c) { return std::isxdigit(c); }

//数字列 [e|E 数字列] の形で最後まで読めるか(指数表記の数値として全体を消費できるか)
bool IsWholeExponentNumber(std::string_view run) {
    size_t i = 0;
    while (i < run.size() && std::isdigit(static_cast<unsigned char>(run[i]))) ++i;
    if (i < run.size() && (run[i] == 'e' || run[i] == 'E')) ++i;
    while (i < run.size() && std::isdigit(static_cast<unsigned char>(run[i]))) ++i;
    return i == run.size();
}

// 空白とコメントを飛ばす
void SkipTrivia(Cursor& c, Diagnostics& diag) {
    for (;;) {
        char ch = c.Peek();
        if (ch == '\0') return;
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            c.Get();
            continue;
        }
        if (ch == '/' && c.Peek(1) == '/') {
            while (!c.Eof() && c.Peek() != '\n') c.Get();
            continue;
        }
        if (ch == '/' && c.Peek(1) == '*') {
            int sl = c.line, sc = c.col;
            c.Get();
            c.Get();
            bool closed = false;
            while (!c.Eof()) {
                if (c.Peek() == '*' && c.Peek(1) == '/') {
                    c.Get();
                    c.Get();
                    closed = true;
                    break;
                }
                c.Get();
            }
            if (!closed) diag.Warn(sl, sc, "unterminated block comment");
            continue;
        }
        return;
    }
}

void Push(std::vector<Token>& out, TokenKind k, std::string text, int line, int col) {
    Token t;
    t.kind = k;
    t.text = std::move(text);
    t.line = line;
    t.column = col;
    out.push_back(std::move(t));
}

// 記号・演算子・色リテラルを読む。該当しなければ false(何も消費しない)
bool TryLexSymbol(Cursor& c, Diagnostics& diag, std::vector<Token>& out, char ch, int tl, int tc) {
    auto push = [&](TokenKind k, std::string text, int line, int col) {
        Push(out, k, std::move(text), line, col);
    };
    switch (ch) {
    case '{': c.Get(); push(TokenKind::LBrace, "{", tl, tc); return true;
    case '}': c.Get(); push(TokenKind::RBrace, "}", tl, tc); return true;
    case '(': c.Get(); push(TokenKind::LParen, "(", tl, tc); return true;
    case ')': c.Get(); push(TokenKind::RParen, ")", tl, tc); return true;
    case '=':
        c.Get();
        if (c.Peek() == '=') { // 条件式の等価演算子 ==
            c.Get();
            push(TokenKind::Operator, "==", tl, tc);
        } else {
            push(TokenKind::Equals, "=", tl, tc);
        }
        return true;
    case ';': c.Get(); push(TokenKind::Semicolon, ";", tl, tc); return true;
    case ',': c.Get(); push(TokenKind::Comma, ",", tl, tc); return true;
    case ':': c.Get(); push(TokenKind::Colon, ":", tl, tc); return true;
    // 色リテラル(#RRGGBB / #AARRGGBB、例: Diffuse = #ff404040;)。'#'の直後に
    // 6か8桁の16進が続く場合のみ専用トークン化し、それ以外は不明文字として
    // 警告付きでスキップする(未知の'#'用法を静かに壊さないため)。
    case '#': {
        size_t k = 0;
        while (IsHexDigit(static_cast<unsigned char>(c.Peek(1 + k)))) ++k;
        if (k == 6 || k == 8) {
            c.Get(); // '#'
            std::string hex;
            for (size_t n = 0; n < k; ++n) hex.push_back(c.Get());
            push(TokenKind::HexColor, std::move(hex), tl, tc);
            return true;
        }
        diag.Warn(tl, tc, "unexpected character '#'");
        c.Get();
        return true;
    }
    // 条件式演算子（If "cond" < 500 || "cond2" 等）は Operator として保持し、
    // パーサ側で条件付きブロックのヘッダとして読み飛ばせるようにする。
    case '<': case '>': case '!': case '|': case '&': {
        std::string op(1, c.Get());
        if ((op[0] == '|' && c.Peek() == '|') ||
            (op[0] == '&' && c.Peek() == '&') ||
            ((op[0] == '<' || op[0] == '>' || op[0] == '!') && c.Peek() == '=') ||
            ((op[0] == '<' || op[0] == '>') && c.Peek() == op[0])) {
            op.push_back(c.Get());
        }
        push(TokenKind::Operator, std::move(op), tl, tc);
        return true;
    }
    // 式演算子（SwitchExpr.cpp が評価）。単項/二項の * / % ~ ^ ? は
    // If/ApplySwitch の条件式にのみ出現し、値側の Number/String/Tuple
    // 構文とは重ならないため単一文字トークンとして扱ってよい。
    // '/' はコメント開始と衝突するため SkipTrivia で先に処理済み
    // （ここに来る '/' は式中の除算演算子のみ）。
    case '*': case '%': case '~': case '^': case '?':
        c.Get();
        push(TokenKind::Operator, std::string(1, ch), tl, tc);
        return true;
    case '/':
        c.Get();
        push(TokenKind::Operator, "/", tl, tc);
        return true;
    default: return false;
    }
}

// 文字列リテラルを読む(開始の '"' は未消費)
void LexString(Cursor& c, Diagnostics& diag, std::vector<Token>& out, int tl, int tc) {
    c.Get();
    std::string val;
    bool closed = false;
    while (!c.Eof()) {
        char q = c.Get();
        if (q == '"') {
            closed = true;
            break;
        }
        if (q == '\\' && !c.Eof()) {
            char e = c.Get();
            switch (e) {
            case 'n': val.push_back('\n'); break;
            case 't': val.push_back('\t'); break;
            case '"': val.push_back('"'); break;
            case '\\': val.push_back('\\'); break;
            default:
                val.push_back('\\');
                val.push_back(e);
                break;
            }
            continue;
        }
        if (q == '\n') break; // 文字列は改行を跨がない
        val.push_back(q);
    }
    if (!closed) diag.Error(tl, tc, "unterminated string literal");
    Push(out, TokenKind::String, std::move(val), tl, tc);
}

// 数値の符号 '+'/'-' は「値の開始位置」でのみ数値に吸収する。直前トークンが
// 値で終わっている場合は式中の二項演算子として単独の Operator トークンにする
// （例: `("_SECOND" + 1) % 2` の '+' が数値"+1"に吸収され消えるのを防ぐ）。
bool TryLexBinarySign(Cursor& c, std::vector<Token>& out, char ch, int tl, int tc) {
    if ((ch != '-' && ch != '+') || out.empty()) return false;
    TokenKind pk = out.back().kind;
    bool prevEndsValue = pk == TokenKind::Number || pk == TokenKind::String ||
                          pk == TokenKind::Identifier || pk == TokenKind::HexAddress ||
                          pk == TokenKind::RParen;
    if (!prevEndsValue) return false;
    c.Get();
    Push(out, TokenKind::Operator, std::string(1, ch), tl, tc);
    return true;
}

// 16進アドレスの先読み。
// 実データの保存アドレスは [0-9A-Fa-f]{16} で直後が識別子文字でない。
// 手作業/エディタ生成のレイアウトでは Address/Link0* が「1」「3c」「10a」のような
// パディング無しの短い16進連番IDで書かれる。'a'/'b'/'c'/'d'/'f' 系の16進文字は
// 数値リテラルに現れないため、桁数によらず無条件に HexAddress として確定できる。
// 'e'/'E' だけは指数表記(1e5等)と衝突しうる。数字と e だけの並びは、指数表記の
// 数値として最後まで読める(1e5, 3e)ときだけ既存の指数解釈に委ね、読めない並び
// (1ee, 3e5e等)は HexAddress として確定する。
bool TryLexHexAddress(Cursor& c, std::vector<Token>& out, int tl, int tc) {
    size_t k = 0;
    bool hasUnambiguousHexLetter = false;
    while (IsHexDigit(static_cast<unsigned char>(c.Peek(k)))) {
        unsigned char hc = static_cast<unsigned char>(c.Peek(k));
        if (!std::isdigit(hc) && hc != 'e' && hc != 'E') hasUnambiguousHexLetter = true;
        ++k;
    }
    bool boundaryOk = k >= 1 && k <= 16 && !IsIdentChar(static_cast<unsigned char>(c.Peek(k))) &&
                      c.Peek(k) != '.';
    // 16進の並びは入力文字列上で連続しているので、コピーせず範囲のまま見る。
    const std::string_view run(c.s.data() + c.pos, k);
    if (!boundaryOk || !(k == 16 || hasUnambiguousHexLetter || !IsWholeExponentNumber(run))) {
        return false;
    }
    std::string hex(run);
    c.SkipNoNewline(k);
    Push(out, TokenKind::HexAddress, std::move(hex), tl, tc);
    return true;
}

// 通常の数値を読む
void LexNumber(Cursor& c, Diagnostics& diag, std::vector<Token>& out, int tl, int tc) {
    std::string num;
    if (c.Peek() == '-' || c.Peek() == '+') num.push_back(c.Get());
    bool hasDigit = false;
    while (std::isdigit(static_cast<unsigned char>(c.Peek()))) {
        num.push_back(c.Get());
        hasDigit = true;
    }
    if (c.Peek() == '.') {
        num.push_back(c.Get());
        while (std::isdigit(static_cast<unsigned char>(c.Peek()))) {
            num.push_back(c.Get());
            hasDigit = true;
        }
    }
    if ((c.Peek() == 'e' || c.Peek() == 'E') && hasDigit) {
        num.push_back(c.Get());
        if (c.Peek() == '-' || c.Peek() == '+') num.push_back(c.Get());
        while (std::isdigit(static_cast<unsigned char>(c.Peek()))) num.push_back(c.Get());
    }
    if (!hasDigit) {
        diag.Error(tl, tc, "malformed number");
        c.Get(); // 前進して無限ループ回避
        return;
    }
    Token t;
    t.kind = TokenKind::Number;
    t.text = num;
    t.number = std::strtod(num.c_str(), nullptr);
    t.line = tl;
    t.column = tc;
    out.push_back(std::move(t));
}

} // namespace

std::vector<Token> Tokenize(const std::string& utf8, Diagnostics& diag) {
    std::vector<Token> out;
    // 実データでは1トークンあたり約6バイト。再確保のたびに全トークンをコピーしないよう先に確保する。
    out.reserve(utf8.size() / 5 + 16);
    Cursor c(utf8);

    for (;;) {
        SkipTrivia(c, diag);
        if (c.Eof()) break;

        int tl = c.line, tc = c.col;
        char ch = c.Peek();

        if (TryLexSymbol(c, diag, out, ch, tl, tc)) continue;

        if (ch == '"') {
            LexString(c, diag, out, tl, tc);
            continue;
        }

        if (IsIdentStart(static_cast<unsigned char>(ch))) {
            size_t n = 0;
            while (IsIdentChar(static_cast<unsigned char>(c.Peek(n)))) ++n;
            std::string id(c.s, c.pos, n);
            c.SkipNoNewline(n);
            Push(out, TokenKind::Identifier, std::move(id), tl, tc);
            continue;
        }

        if (TryLexBinarySign(c, out, ch, tl, tc)) continue;

        // 数値 or hex アドレス
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' ||
            ch == '.') {
            if (std::isdigit(static_cast<unsigned char>(ch)) &&
                TryLexHexAddress(c, out, tl, tc)) {
                continue;
            }
            LexNumber(c, diag, out, tl, tc);
            continue;
        }

        // 未知文字はスキップして継続
        diag.Warn(tl, tc, std::string("unexpected character '") + ch + "'");
        c.Get();
    }

    Push(out, TokenKind::End, "", c.line, c.col);
    return out;
}

} // namespace rs::legacy_pi
