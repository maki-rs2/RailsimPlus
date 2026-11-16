#include "SwitchExprParser.h"

#include "SwitchExprLexer.h"

#include <array>
#include <vector>

namespace rs::legacy_pi::switch_expr_detail {

namespace {

// 再帰下降パーサ + 優先順位法。値の評価(resolve呼び出し)は行わず、AST(ExprNode
// 木)を組み立てるだけにする(構造は元の評価版と同じ優先順位・結合規則)。
// 優先順位（Help pi_sym_expression.html 準拠、数字が小さいほど強く束縛。
// 同順位は左結合、? : のみ右結合）:
//   1. ( )
//   2. 単項 + - ! ~
//   3. * / %
//   4. + -
//   5. << >>
//   6. < > <= >=
//   7. == !=
//   8. &
//   9. ^
//  10. |
//  11. &&（非0判定。Help原文は「いずれも1なら1」だがC同様の非0判定で実装）
//  12. ||
//  13. ?: （右結合）

struct BinaryOpPrecedence {
    const char* op;
    int         precedence;
};

// Help記載の優先順位（上記コメント）を、優先順位登り法用に大きいほど強く
// 束縛する向きの数値へ読み替えたもの。
constexpr std::array<BinaryOpPrecedence, 18> kBinOps = {{
    {"||", 1},                         // 12. ||
    {"&&", 2},                         // 11. &&
    {"|", 3},                          // 10. |
    {"^", 4},                          // 9. ^
    {"&", 5},                          // 8. &
    {"==", 6}, {"!=", 6},             // 7. == !=
    {"<", 7}, {">", 7}, {"<=", 7}, {">=", 7}, // 6. < > <= >=
    {"<<", 8}, {">>", 8},             // 5. << >>
    {"+", 9}, {"-", 9},               // 4. + -
    {"*", 10}, {"/", 10}, {"%", 10}, // 3. * / %（右辺0の / % は 0 を返す仕様。除算実行は Eval 時）
}};

int FindBinaryPrecedence(const ExprTok& tok) {
    if (tok.kind != ExprTokKind::Op) return 0;
    for (const BinaryOpPrecedence& binaryOp : kBinOps) {
        if (tok.text == binaryOp.op) return binaryOp.precedence;
    }
    return 0;
}

class ExprParser {
public:
    explicit ExprParser(const std::vector<ExprTok>& toks) : toks_(toks) {}

    NodePtr Parse(std::string& err) {
        NodePtr n = ParseTernary(err);
        if (!err.empty()) return nullptr;
        if (!At(ExprTokKind::End)) {
            err = "unexpected trailing token '" + Cur().text + "' in expression";
            return nullptr;
        }
        return n;
    }

private:
    const std::vector<ExprTok>& toks_;
    size_t                      pos_ = 0;

    const ExprTok& Cur() const { return toks_[pos_]; }
    bool At(ExprTokKind k) const { return Cur().kind == k; }
    bool AtOp(const char* op) const { return Cur().kind == ExprTokKind::Op && Cur().text == op; }
    void Advance() {
        if (pos_ + 1 < toks_.size()) ++pos_;
    }

    // 13. ?: （右結合）
    NodePtr ParseTernary(std::string& err) {
        NodePtr cond = ParseBinary(1, err);
        if (!err.empty()) return nullptr;
        if (At(ExprTokKind::Question)) {
            Advance();
            NodePtr thenN = ParseTernary(err); // 右結合: then 側も ?: 全体を許容
            if (!err.empty()) return nullptr;
            if (!At(ExprTokKind::Colon)) {
                err = "expected ':' in ternary expression";
                return nullptr;
            }
            Advance();
            NodePtr elseN = ParseTernary(err);
            if (!err.empty()) return nullptr;
            return MakeTernary(std::move(cond), std::move(thenN), std::move(elseN));
        }
        return cond;
    }

    NodePtr ParseBinary(int minPrec, std::string& err) {
        NodePtr lhs = ParseUnary(err);
        if (!err.empty()) return nullptr;

        for (;;) {
            int prec = FindBinaryPrecedence(Cur());
            if (prec < minPrec) break;

            std::string op = Cur().text;
            Advance();
            NodePtr rhs = ParseBinary(prec + 1, err);
            if (!err.empty()) return nullptr;
            lhs = MakeBinary(std::move(op), std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    // 2. 単項 + - ! ~
    NodePtr ParseUnary(std::string& err) {
        if (AtOp("+")) {
            Advance();
            return ParseUnary(err); // 単項+は恒等(ノード生成不要)
        }
        if (AtOp("-")) {
            Advance();
            NodePtr v = ParseUnary(err);
            if (!err.empty()) return nullptr;
            return MakeUnary("-", std::move(v));
        }
        if (AtOp("!")) {
            Advance();
            NodePtr v = ParseUnary(err);
            if (!err.empty()) return nullptr;
            return MakeUnary("!", std::move(v));
        }
        if (AtOp("~")) {
            Advance();
            NodePtr v = ParseUnary(err);
            if (!err.empty()) return nullptr;
            return MakeUnary("~", std::move(v));
        }
        return ParsePrimary(err);
    }

    // 1. ( ) / 定数 / スイッチ参照
    NodePtr ParsePrimary(std::string& err) {
        if (At(ExprTokKind::LParen)) {
            Advance();
            NodePtr v = ParseTernary(err);
            if (!err.empty()) return nullptr;
            if (!At(ExprTokKind::RParen)) {
                err = "expected ')' in expression";
                return nullptr;
            }
            Advance();
            return v;
        }
        if (At(ExprTokKind::Number)) {
            int32_t v = Cur().number;
            Advance();
            return MakeConst(v);
        }
        if (At(ExprTokKind::String)) {
            std::string name = Cur().text;
            Advance();
            return MakeSwitchRef(std::move(name));
        }
        err = "unexpected token '" + Cur().text + "' in expression";
        return nullptr;
    }
};

} // namespace

NodePtr ParseSwitchExpr(const std::string& expr, std::string& err) {
    std::vector<ExprTok> toks;
    ExprLexer lexer(expr);
    if (!lexer.Tokenize(toks, err)) {
        return nullptr;
    }
    ExprParser parser(toks);
    return parser.Parse(err);
}

} // namespace rs::legacy_pi::switch_expr_detail
