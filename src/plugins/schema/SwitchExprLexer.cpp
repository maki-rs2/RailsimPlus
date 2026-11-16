#include "SwitchExprLexer.h"

#include <cctype>
#include <cstdlib>

namespace rs::legacy_pi::switch_expr_detail {

bool ExprLexer::Tokenize(std::vector<ExprTok>& out, std::string& err) {
    size_t i = 0;
    const size_t n = s_.size();
    while (i < n) {
        char ch = s_[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            ++i;
            continue;
        }
        if (ch == '(') {
            out.push_back({ExprTokKind::LParen, 0, "("});
            ++i;
            continue;
        }
        if (ch == ')') {
            out.push_back({ExprTokKind::RParen, 0, ")"});
            ++i;
            continue;
        }
        if (ch == '?') {
            out.push_back({ExprTokKind::Question, 0, "?"});
            ++i;
            continue;
        }
        if (ch == ':') {
            out.push_back({ExprTokKind::Colon, 0, ":"});
            ++i;
            continue;
        }
        if (ch == '"') {
            size_t j = i + 1;
            std::string val;
            bool closed = false;
            while (j < n) {
                if (s_[j] == '"') {
                    closed = true;
                    ++j;
                    break;
                }
                val.push_back(s_[j]);
                ++j;
            }
            if (!closed) {
                err = "unterminated string literal in expression";
                return false;
            }
            out.push_back({ExprTokKind::String, 0, val});
            i = j;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            size_t j = i;
            while (j < n && std::isdigit(static_cast<unsigned char>(s_[j]))) ++j;
            // 小数点以下も読み取って0方向へ切り捨てる。式の評価自体は整数のみだが、
            // 実データの条件式には小数リテラルが書かれることがあり、ここで構文エラーに
            // すると条件式が丸ごと恒偽になってそのブロックが永久に発動しなくなる。
            // RailSim II の定数読み取りも小数を受理し整数へキャストする(0.01は0になる)。
            bool hasFraction = false;
            if (j < n && s_[j] == '.' && j + 1 < n &&
                std::isdigit(static_cast<unsigned char>(s_[j + 1]))) {
                hasFraction = true;
                ++j;
                while (j < n && std::isdigit(static_cast<unsigned char>(s_[j]))) ++j;
            }
            const std::string num = s_.substr(i, j - i);
            ExprTok t;
            t.kind = ExprTokKind::Number;
            t.number = hasFraction
                           ? static_cast<int32_t>(std::strtod(num.c_str(), nullptr))
                           : static_cast<int32_t>(std::strtol(num.c_str(), nullptr, 10));
            out.push_back(t);
            i = j;
            continue;
        }
        // 演算子（複数文字を優先してマッチ）。
        static const char* kMulti[] = {"==", "!=", "<=", ">=", "&&", "||", "<<", ">>"};
        bool matched = false;
        for (const char* op : kMulti) {
            size_t len = std::string(op).size();
            if (i + len <= n && s_.compare(i, len, op) == 0) {
                out.push_back({ExprTokKind::Op, 0, op});
                i += len;
                matched = true;
                break;
            }
        }
        if (matched) continue;
        static const std::string kSingle = "+-*/%<>!~^&|";
        if (kSingle.find(ch) != std::string::npos) {
            out.push_back({ExprTokKind::Op, 0, std::string(1, ch)});
            ++i;
            continue;
        }
        err = std::string("unexpected character '") + ch + "' in expression";
        return false;
    }
    out.push_back({ExprTokKind::End, 0, ""});
    return true;
}

} // namespace rs::legacy_pi::switch_expr_detail
