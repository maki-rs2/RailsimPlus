#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace rs::legacy_pi::switch_expr_detail {

// 条件式のパース結果(AST)。1回パースすれば resolver が変わっても(=スイッチ値が
// 変わっても)再利用できる。EvalSwitchExpr が式文字列単位でキャッシュすることで、
// 「毎フレーム・毎エントリで同じ条件式文字列を再字句解析+再構文解析する」
// コストを無くす(発車標プラグインでは EvalSwitchExpr が
// 543回/frame呼ばれ、区間時間の89%を占めた)。
// 値はスイッチ参照(resolve呼び出し)を含むため Eval 時にしか確定しない
// （ここでは構造だけを1回組み立てて保持する）。
struct ExprNode {
    enum class Kind { Const, SwitchRef, Unary, Binary, Ternary } kind = Kind::Const;
    int32_t constVal = 0;
    std::string switchName;  // SwitchRef
    std::string op;          // Unary/Binary の演算子記号
    std::unique_ptr<ExprNode> a, b, c; // Unary:a / Binary:a,b / Ternary:cond=a,then=b,else=c
};
using NodePtr = std::unique_ptr<ExprNode>;

NodePtr MakeConst(int32_t v);
NodePtr MakeSwitchRef(std::string name);
NodePtr MakeUnary(std::string op, NodePtr a);
NodePtr MakeBinary(std::string op, NodePtr a, NodePtr b);
NodePtr MakeTernary(NodePtr cond, NodePtr thenN, NodePtr elseN);

} // namespace rs::legacy_pi::switch_expr_detail
