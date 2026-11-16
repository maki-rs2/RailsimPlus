#include "SwitchExprAst.h"

namespace rs::legacy_pi::switch_expr_detail {

NodePtr MakeConst(int32_t v) {
    NodePtr n = std::make_unique<ExprNode>();
    n->kind = ExprNode::Kind::Const;
    n->constVal = v;
    return n;
}
NodePtr MakeSwitchRef(std::string name) {
    NodePtr n = std::make_unique<ExprNode>();
    n->kind = ExprNode::Kind::SwitchRef;
    n->switchName = std::move(name);
    return n;
}
NodePtr MakeUnary(std::string op, NodePtr a) {
    NodePtr n = std::make_unique<ExprNode>();
    n->kind = ExprNode::Kind::Unary;
    n->op = std::move(op);
    n->a = std::move(a);
    return n;
}
NodePtr MakeBinary(std::string op, NodePtr a, NodePtr b) {
    NodePtr n = std::make_unique<ExprNode>();
    n->kind = ExprNode::Kind::Binary;
    n->op = std::move(op);
    n->a = std::move(a);
    n->b = std::move(b);
    return n;
}
NodePtr MakeTernary(NodePtr cond, NodePtr thenN, NodePtr elseN) {
    NodePtr n = std::make_unique<ExprNode>();
    n->kind = ExprNode::Kind::Ternary;
    n->a = std::move(cond);
    n->b = std::move(thenN);
    n->c = std::move(elseN);
    return n;
}

} // namespace rs::legacy_pi::switch_expr_detail
