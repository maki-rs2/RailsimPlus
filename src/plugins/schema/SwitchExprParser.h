#pragma once

#include "SwitchExprAst.h"

#include <string>

namespace rs::legacy_pi::switch_expr_detail {

// 式文字列を字句解析+構文解析してASTへ変換する。失敗時は nullptr を返し err に
// エラーメッセージを入れる。
NodePtr ParseSwitchExpr(const std::string& expr, std::string& err);

} // namespace rs::legacy_pi::switch_expr_detail
