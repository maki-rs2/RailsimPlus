#include "Parser.h"

#include "Lexer.h"
#include "../../../platform/text/Encoding.h"

namespace rs::legacy_pi {

namespace {

class Parser {
public:
    Parser(std::vector<Token> toks, Diagnostics& diag)
        : toks_(std::move(toks)), diag_(diag) {}

    PiDocument ParseDocument() {
        PiDocument doc;
        while (!At(TokenKind::End)) {
            // トップレベルは Identifier で始まるブロックのみ想定
            if (At(TokenKind::Identifier)) {
                PiBlock b;
                if (ParseBlockOrAssignmentInto(b, doc.nodes)) {
                    // parseBlockOrAssignmentInto はブロック/代入を親に積む。
                    // トップレベルの代入は稀だが許容する。
                }
            } else {
                const Token& t = Cur();
                diag_.Warn(t.line, t.column,
                           "unexpected token at top level: '" + t.text + "'");
                Advance();
            }
        }
        return doc;
    }

private:
    const Token& Cur() const { return toks_[pos_]; }
    const Token& Peek(size_t off = 1) const {
        size_t i = pos_ + off;
        return i < toks_.size() ? toks_[i] : toks_.back();
    }
    // 消費済みトークンの文字列は読み返さないので、コピーせず取り出す。
    std::string TakeText() { return std::move(toks_[pos_].text); }
    bool At(TokenKind k) const { return Cur().kind == k; }
    void Advance() {
        if (pos_ + 1 < toks_.size()) ++pos_;
    }
    bool Accept(TokenKind k) {
        if (At(k)) {
            Advance();
            return true;
        }
        return false;
    }

    // 親コンテナ (doc.nodes 相当) にブロック/代入を追加。
    // 呼び出し側は 現在トークンが Identifier の状態で呼ぶ。
    bool ParseBlockOrAssignmentInto(PiBlock& /*scratch*/,
                                    std::vector<PiBlock>& topNodes) {
        // トップレベル用の薄いラッパ。ブロックのみ受け付ける。
        if (Peek().kind == TokenKind::LBrace ||
            Peek().kind == TokenKind::String) {
            PiBlock b = ParseBlock();
            topNodes.push_back(std::move(b));
            return true;
        }
        // トップレベルの裸の Identifier（代入/ゴミ）はブロックでないので
        // 1 トークンだけ読み飛ばして継続（後続ブロックを巻き込まない）。
        const Token& t = Cur();
        diag_.Warn(t.line, t.column,
                   "top-level token ignored: '" + t.text + "'");
        Advance();
        return false;
    }

    // 現在トークン = ブロック名 Identifier。 Name Label? { members }
    PiBlock ParseBlock() {
        PiBlock b;
        b.name = TakeText();
        Advance();
        if (At(TokenKind::String)) {
            b.label = TakeText();
            Advance();
        }
        if (!Accept(TokenKind::LBrace)) {
            const Token& t = Cur();
            diag_.Error(t.line, t.column,
                        "expected '{' after block '" + b.name + "'");
            return b;
        }
        ParseMembers(b);
        if (!Accept(TokenKind::RBrace)) {
            const Token& t = Cur();
            diag_.Error(t.line, t.column,
                        "expected '}' to close block '" + b.name + "'");
        }
        return b;
    }

    // 現在トークン=Identifier。その後に条件式トークン列が続き '{' で本体が
    // 始まるか先読み判定する。'=' や ';' が先に来たら代入なので false。
    bool LooksLikeConditionalBlock() const {
        size_t i = pos_ + 1;
        // Else のように直後がすぐ '{' の場合も条件付きブロック扱いにするが、
        // それは既存の単純ブロック判定で拾われるためここには来ない。
        int guard = 0;
        while (i < toks_.size() && guard < 256) {
            TokenKind k = toks_[i].kind;
            if (k == TokenKind::LBrace) return true;
            if (k == TokenKind::Equals || k == TokenKind::Semicolon ||
                k == TokenKind::RBrace || k == TokenKind::End ||
                k == TokenKind::Colon)
                return false;
            // 条件式に現れうるトークンのみ許容（Operator/String/Number/Ident/
            // Paren/Comma）。それ以外が来たら条件式ではない。
            if (k != TokenKind::Operator && k != TokenKind::String &&
                k != TokenKind::Number && k != TokenKind::Identifier &&
                k != TokenKind::LParen && k != TokenKind::RParen &&
                k != TokenKind::Comma && k != TokenKind::HexAddress)
                return false;
            ++i;
            ++guard;
        }
        return false;
    }

    // Name <condition tokens> { members }
    PiBlock ParseConditionalBlock() {
        PiBlock b;
        b.name = Cur().text; // If / ApplySwitch / Else など
        Advance();
        // 条件式トークンを不透明ラベルとして連結
        std::string cond;
        while (!At(TokenKind::LBrace) && !At(TokenKind::End)) {
            if (!cond.empty() && Cur().kind != TokenKind::Comma) cond += ' ';
            if (Cur().kind == TokenKind::String)
                cond += "\"" + Cur().text + "\"";
            else
                cond += Cur().text;
            Advance();
        }
        b.label = cond;
        b.labelIsExpression = true;
        if (!Accept(TokenKind::LBrace)) return b;
        ParseMembers(b);
        if (!Accept(TokenKind::RBrace)) {
            const Token& t = Cur();
            diag_.Error(t.line, t.column,
                        "expected '}' to close conditional block '" + b.name + "'");
        }
        return b;
    }

    void ParseMembers(PiBlock& b) {
        while (!At(TokenKind::RBrace) && !At(TokenKind::End)) {
            if (At(TokenKind::Identifier)) {
                // Case n1, n2, ...:  分岐ラベル（複数値カンマ区切り）
                if (Cur().text == "Case" && Peek().kind == TokenKind::Number) {
                    std::vector<int> caseVals;
                    Advance(); // Case
                    caseVals.push_back(static_cast<int>(Cur().number));
                    Advance(); // 先頭値
                    // Case n, n, n: の複数値リストを全て保持
                    while (Accept(TokenKind::Comma)) {
                        if (At(TokenKind::Number)) {
                            caseVals.push_back(static_cast<int>(Cur().number));
                            Advance();
                        }
                    }
                    if (!Accept(TokenKind::Colon)) {
                        const Token& t = Cur();
                        diag_.Warn(t.line, t.column, "expected ':' after Case label");
                    }
                    PiMember m;
                    m.kind = PiMember::Kind::CaseLabel;
                    m.caseValue = caseVals.empty() ? 0 : caseVals.front();
                    m.caseValues = std::move(caseVals);
                    b.members.push_back(std::move(m));
                    continue;
                }
                // Default:  ApplySwitch 内の既定分岐（Case の対）
                if (Cur().text == "Default" && Peek().kind == TokenKind::Colon) {
                    Advance(); // Default
                    Advance(); // ':'
                    PiMember m;
                    m.kind = PiMember::Kind::CaseLabel;
                    m.isDefault = true;
                    b.members.push_back(m);
                    continue;
                }
                // Block: Name { ... } または Name "label" { ... }
                // 「Name "文字列" { ... }」は通常の名前付きブロック(Body "Bogie1"等)と、
                // If の条件が単一の引用スイッチ名だけの場合(If "回転"{...}、演算子無し)の
                // 両方に構文上一致してしまう。If は常に条件式(不透明ラベル)として扱う
                // 必要があるため、If はこの単純ブロック判定より先に除外する
                // (混同すると If "回転"{...} のラベルが引用符を失ってただの
                //  "回転" になり、EvalSwitchExpr が識別子として字句解析できず
                //  常に非activeになる)。
                const bool isIfBlock = Cur().text == "If";
                if (!isIfBlock &&
                    (Peek().kind == TokenKind::LBrace ||
                     (Peek().kind == TokenKind::String && Peek(2).kind == TokenKind::LBrace))) {
                    size_t idx = b.blocks.size();
                    b.blocks.push_back(ParseBlock());
                    PiMember m;
                    m.kind = PiMember::Kind::Block;
                    m.index = idx;
                    b.members.push_back(m);
                    continue;
                }
                // 条件付きブロック: If "cond" < 500 || "cond2" { ... } や
                // ApplySwitch "s" { ... }、Else{ ... } 等。識別子の後に（代入や
                // 単純ブロックにならない）条件式トークン列が続き、その先に '{' が
                // 来る形。条件式は不透明ラベルとして保持し、本体はブロックとして
                // 取り込む（中の ChangeModel/ChangeTexture 等を確実に拾うため）。
                if (isIfBlock || LooksLikeConditionalBlock()) {
                    size_t idx = b.blocks.size();
                    b.blocks.push_back(ParseConditionalBlock());
                    PiMember m;
                    m.kind = PiMember::Kind::Block;
                    m.index = idx;
                    b.members.push_back(m);
                    continue;
                }
                // Assignment: Name = v,v,...; または Name; (フラグ)
                ParseAssignment(b);
                continue;
            }
            // 想定外トークンはスキップ（継続）
            const Token& t = Cur();
            diag_.Warn(t.line, t.column,
                       "skipping unexpected token '" + t.text + "'");
            Advance();
        }
    }

    void ParseAssignment(PiBlock& b) {
        PiAssignment a;
        a.key = TakeText();
        Advance();
        if (Accept(TokenKind::Semicolon)) {
            // フラグ代入
            size_t idx = b.assignments.size();
            b.assignments.push_back(std::move(a));
            PiMember m;
            m.kind = PiMember::Kind::Assignment;
            m.index = idx;
            b.members.push_back(m);
            return;
        }
        if (!Accept(TokenKind::Equals)) {
            const Token& t = Cur();
            diag_.Warn(t.line, t.column,
                       "expected '=' or ';' after key '" + a.key + "'");
            SkipToSemicolonOrBrace();
            return;
        }
        // 値リスト
        a.values.push_back(ParseValue());
        while (Accept(TokenKind::Comma)) {
            a.values.push_back(ParseValue());
        }
        if (!Accept(TokenKind::Semicolon)) {
            const Token& t = Cur();
            diag_.Warn(t.line, t.column,
                       "expected ';' after assignment '" + a.key + "'");
        }
        size_t idx = b.assignments.size();
        b.assignments.push_back(std::move(a));
        PiMember m;
        m.kind = PiMember::Kind::Assignment;
        m.index = idx;
        b.members.push_back(m);
    }

    PiValue ParseValue() {
        Token& t = toks_[pos_];
        PiValue v;
        switch (t.kind) {
        case TokenKind::Number:
            v.kind = PiValue::Kind::Number;
            v.number = t.number;
            v.text = std::move(t.text);
            Advance();
            return v;
        case TokenKind::String:
            v.kind = PiValue::Kind::String;
            v.text = std::move(t.text);
            Advance();
            return v;
        case TokenKind::HexAddress:
            v.kind = PiValue::Kind::HexAddr;
            v.text = std::move(t.text);
            Advance();
            return v;
        case TokenKind::HexColor:
            v.kind = PiValue::Kind::HexColor;
            v.text = std::move(t.text); // '#'を除いた6/8桁hex
            Advance();
            return v;
        case TokenKind::Identifier:
            v.kind = PiValue::Kind::Ident;
            v.text = std::move(t.text);
            Advance();
            return v;
        case TokenKind::LParen:
            return ParseTuple();
        default:
            diag_.Warn(t.line, t.column, "unexpected value token '" + t.text + "'");
            Advance();
            return v;
        }
    }

    PiValue ParseTuple() {
        PiValue v;
        v.kind = PiValue::Kind::Tuple;
        Advance(); // (
        if (!At(TokenKind::RParen)) {
            v.tuple.push_back(ParseValue());
            while (Accept(TokenKind::Comma)) v.tuple.push_back(ParseValue());
        }
        if (!Accept(TokenKind::RParen)) {
            const Token& t = Cur();
            diag_.Warn(t.line, t.column, "expected ')' to close tuple");
        }
        return v;
    }

    void SkipToSemicolonOrBrace() {
        while (!At(TokenKind::End) && !At(TokenKind::Semicolon) &&
               !At(TokenKind::RBrace)) {
            Advance();
        }
        Accept(TokenKind::Semicolon);
    }

    std::vector<Token> toks_;
    size_t             pos_ = 0;
    Diagnostics&       diag_;
};

} // namespace

PiDocument Parse(const std::string& utf8, Diagnostics& diag) {
    std::vector<Token> toks = Tokenize(utf8, diag);
    Parser p(std::move(toks), diag);
    return p.ParseDocument();
}

PiDocument ParseFile(const std::wstring& path, Diagnostics& diag) {
    bool ok = false;
    std::string utf8 = platform_text::ReadFileAsUtf8(path, ok);
    if (!ok) {
        diag.Error(0, 0, "cannot open file");
        return {};
    }
    return Parse(utf8, diag);
}

} // namespace rs::legacy_pi
