#include "SwitchExpr.h"

#include "SwitchExprAst.h"
#include "SwitchExprParser.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace rs::legacy_pi {

namespace {

using switch_expr_detail::ExprNode;
using switch_expr_detail::NodePtr;
using switch_expr_detail::ParseSwitchExpr;

// AST を resolver で評価する。resolve はスイッチ値参照のみ(副作用無し)なので、
// 元実装同様 &&/|| も短絡評価せず両辺を評価する(値そのものは変わらない)。
int32_t EvalNode(const ExprNode* n, const SwitchResolver& resolve) {
    if (!n) return 0;
    switch (n->kind) {
    case ExprNode::Kind::Const:
        return n->constVal;
    case ExprNode::Kind::SwitchRef:
        return resolve ? resolve(n->switchName) : 0;
    case ExprNode::Kind::Unary: {
        int32_t v = EvalNode(n->a.get(), resolve);
        if (n->op == "-") return static_cast<int32_t>(0u - static_cast<uint32_t>(v));
        if (n->op == "!") return v == 0 ? 1 : 0;
        return ~v; // "~"
    }
    case ExprNode::Kind::Binary: {
        int32_t lhs = EvalNode(n->a.get(), resolve);
        int32_t rhs = EvalNode(n->b.get(), resolve);
        const std::string& op = n->op;
        if (op == "||") return (lhs != 0 || rhs != 0) ? 1 : 0;
        if (op == "&&") return (lhs != 0 && rhs != 0) ? 1 : 0;
        if (op == "|") return lhs | rhs;
        if (op == "^") return lhs ^ rhs;
        if (op == "&") return lhs & rhs;
        if (op == "==") return (lhs == rhs) ? 1 : 0;
        if (op == "!=") return (lhs != rhs) ? 1 : 0;
        if (op == "<") return (lhs < rhs) ? 1 : 0;
        if (op == ">") return (lhs > rhs) ? 1 : 0;
        if (op == "<=") return (lhs <= rhs) ? 1 : 0;
        if (op == ">=") return (lhs >= rhs) ? 1 : 0;
        if (op == "<<")
            return (rhs >= 0 && rhs < 32)
                       ? static_cast<int32_t>(static_cast<uint32_t>(lhs) << rhs)
                       : 0;
        if (op == ">>") return (rhs >= 0 && rhs < 32) ? (lhs >> rhs) : (lhs < 0 ? -1 : 0);
        if (op == "+")
            return static_cast<int32_t>(static_cast<uint32_t>(lhs) + static_cast<uint32_t>(rhs));
        if (op == "-")
            return static_cast<int32_t>(static_cast<uint32_t>(lhs) - static_cast<uint32_t>(rhs));
        if (op == "*")
            return static_cast<int32_t>(static_cast<uint32_t>(lhs) * static_cast<uint32_t>(rhs));
        // INT_MIN / -1はint32の範囲を超え(int32二の補数wrapの仕様どおりINT_MINへ戻す)、
        // 素のint演算に投げるとUBになる。
        // INT_MIN % -1は数学的に0だが、同じUBを避けるため除算経由にせず先に0を返す。
        if (op == "/") {
            if (rhs == 0) return 0;
            if (lhs == INT32_MIN && rhs == -1) return INT32_MIN;
            return lhs / rhs;
        }
        if (op == "%") {
            if (rhs == 0) return 0;
            if (lhs == INT32_MIN && rhs == -1) return 0;
            return lhs % rhs;
        }
        return 0;
    }
    case ExprNode::Kind::Ternary: {
        int32_t cond = EvalNode(n->a.get(), resolve);
        return cond != 0 ? EvalNode(n->b.get(), resolve) : EvalNode(n->c.get(), resolve);
    }
    }
    return 0;
}

// 式文字列 → パース結果のキャッシュ(プロセス内・アプリはシングルスレッド
// 前提。std::thread/std::async は使っていない)。同一プラグイン内で
// 同じ条件式文字列が多数のcustomizerから共有される(発車標プラグインでは
// 最大chain深さ5・543回/frame呼び出しになる)ため、
// 「呼び出しのたびに毎回再字句解析+再構文解析する」コストをここで無くす。
struct CompiledProgram {
    bool ok = false;
    std::string error;
    NodePtr root;
};

CompiledProgram CompileExprNoCache(const std::string& expr) {
    CompiledProgram prog;
    std::string err;
    NodePtr root = ParseSwitchExpr(expr, err);
    if (!root) {
        prog.ok = false;
        prog.error = err.empty() ? "parse error" : err;
        return prog;
    }
    prog.ok = true;
    prog.root = std::move(root);
    return prog;
}

const CompiledProgram& CompileExprCached(const std::string& expr) {
    static std::unordered_map<std::string, CompiledProgram> cache;
    auto it = cache.find(expr);
    if (it != cache.end()) return it->second;
    auto [ins, inserted] = cache.emplace(expr, CompileExprNoCache(expr));
    return ins->second;
}

// 以下、AnalyzeSwitchComparisons の実装補助。switchName の出現位置がAST上で
// 「比較演算子の直接の片辺として定数と比較される」形かどうかを判定する
// (CamDistBands.cpp の量子化安全性判定から利用される)。

bool ContainsSwitchRef(const ExprNode* n, const std::string& switchName) {
    if (!n) return false;
    switch (n->kind) {
    case ExprNode::Kind::Const:
        return false;
    case ExprNode::Kind::SwitchRef:
        return n->switchName == switchName;
    case ExprNode::Kind::Unary:
        return ContainsSwitchRef(n->a.get(), switchName);
    case ExprNode::Kind::Binary:
        return ContainsSwitchRef(n->a.get(), switchName) || ContainsSwitchRef(n->b.get(), switchName);
    case ExprNode::Kind::Ternary:
        return ContainsSwitchRef(n->a.get(), switchName) || ContainsSwitchRef(n->b.get(), switchName) ||
               ContainsSwitchRef(n->c.get(), switchName);
    }
    return false;
}

bool IsComparisonOp(const std::string& op) {
    return op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=";
}

// switchName についての比較を再帰的に集める。switchNameが定数との比較の直接の片辺に
// 現れる限り、式全体の値はその比較の真偽だけで決まり、外側の演算子が何であっても
// 閾値で区切った帯の中では変わらない(RailSim IIの条件式は論理積を`&`で書くものが多い)。
// そこで外側は演算子を問わず再帰し、比較の外にswitchNameが裸で現れたとき
// (SwitchRefの枝)と、比較の片辺が定数でも単独の参照でもないときだけ
// hasUnsupportedReference=true にする。
void ScanSwitchComparisonsNode(const ExprNode* n, const std::string& switchName,
                               SwitchComparisonBands& result) {
    if (!n) return;
    switch (n->kind) {
    case ExprNode::Kind::Const:
        return;
    case ExprNode::Kind::SwitchRef:
        if (n->switchName == switchName) {
            result.referenced = true;
            result.hasUnsupportedReference = true;
        }
        return;
    case ExprNode::Kind::Unary:
        ScanSwitchComparisonsNode(n->a.get(), switchName, result);
        return;
    case ExprNode::Kind::Binary:
        if (IsComparisonOp(n->op)) {
            const ExprNode* constSide = nullptr;
            if (n->a->kind == ExprNode::Kind::SwitchRef && n->a->switchName == switchName &&
                n->b->kind == ExprNode::Kind::Const) {
                constSide = n->b.get();
            } else if (n->b->kind == ExprNode::Kind::SwitchRef && n->b->switchName == switchName &&
                       n->a->kind == ExprNode::Kind::Const) {
                constSide = n->a.get();
            }
            if (constSide) {
                result.referenced = true;
                result.thresholds.push_back(constSide->constVal);
                return;
            }
            if (ContainsSwitchRef(n->a.get(), switchName) || ContainsSwitchRef(n->b.get(), switchName)) {
                result.referenced = true;
                result.hasUnsupportedReference = true;
            }
            return;
        }
        ScanSwitchComparisonsNode(n->a.get(), switchName, result);
        ScanSwitchComparisonsNode(n->b.get(), switchName, result);
        return;
    case ExprNode::Kind::Ternary:
        ScanSwitchComparisonsNode(n->a.get(), switchName, result);
        ScanSwitchComparisonsNode(n->b.get(), switchName, result);
        ScanSwitchComparisonsNode(n->c.get(), switchName, result);
        return;
    }
}

} // namespace

ExprResult EvalSwitchExpr(const std::string& expr, const SwitchResolver& resolve) {
    ExprResult result;
    const CompiledProgram& prog = CompileExprCached(expr);
    if (!prog.ok) {
        result.ok = false;
        result.error = prog.error;
        return result;
    }
    result.ok = true;
    result.value = EvalNode(prog.root.get(), resolve);
    return result;
}

bool ExprReferencesSwitch(const std::string& expr, const std::string& switchName) {
    const CompiledProgram& prog = CompileExprCached(expr);
    if (!prog.ok) {
        // パース不能な式は、switchNameの文字列が含まれるかどうかだけで安全側に倒す。
        return expr.find(switchName) != std::string::npos;
    }
    return ContainsSwitchRef(prog.root.get(), switchName);
}

SwitchComparisonBands AnalyzeSwitchComparisons(const std::string& expr, const std::string& switchName) {
    SwitchComparisonBands result;
    const CompiledProgram& prog = CompileExprCached(expr);
    if (!prog.ok) {
        // パース不能な式は、switchNameの文字列が含まれるかどうかだけで安全側に倒す。
        if (expr.find(switchName) != std::string::npos) {
            result.referenced = true;
            result.hasUnsupportedReference = true;
        }
        return result;
    }
    ScanSwitchComparisonsNode(prog.root.get(), switchName, result);
    return result;
}

} // namespace rs::legacy_pi
