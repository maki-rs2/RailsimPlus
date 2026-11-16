//
// SwitchExpr.h - RailSim2 互換 PI 条件式（If/ApplySwitch ラベル）の評価器
//
// PiBlock::label には「空白区切りで連結された不透明文字列」として条件式が
// 格納されている（Parser.cpp parseConditionalBlock() 参照）。本モジュールは
// この文字列を再度トークナイズし、公式 Help（pi_sym_expression.html）記載の
// 優先順位・結合規則に従って再帰下降で評価する。
//
// 演算はすべて整数(int32_t)。定数は整数のみ。"スイッチ名" 形式の文字列は
// スイッチ値参照として resolver に問い合わせる。
//
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace rs::legacy_pi {

// スイッチ名(引用符内の文字列)→値 の解決コールバック。
// 未知の名前は 0 を返す実装を推奨（呼び出し側の責務）。
using SwitchResolver = std::function<int32_t(const std::string&)>;

struct ExprResult {
    int32_t     value = 0;
    bool        ok = false;
    std::string error;
};

// PiBlock::label に格納された不透明条件式文字列を評価する。
// 構文エラー時は ok=false を返し value=0（RailSim II は寛容なので呼び出し側は
// 0 扱いで継続してよい）。
ExprResult EvalSwitchExpr(const std::string& expr, const SwitchResolver& resolve);

// switchName が式中で「比較演算子(<,>,<=,>=,==,!=)の直接の片辺として定数と比較される」
// 形でのみ現れているかを解析する。&&/||/単項!は区別せずそのまま再帰するため、
// `"A" < 40 && "B" < 100` のような複合条件でも「B」についての比較だけを問題なく
// 抽出できる。算術式の中(例: `"B" + 1 < 100`)や三項演算子の値側など、比較の
// 直接の片辺以外の形でswitchNameが現れた場合は hasUnsupportedReference=true で
// 安全側に倒す(呼び出し側はこの式全体についてswitchNameの量子化/最適化を諦めること)。
// 比較の外側の演算子は問わない(`"A"==1 & "B"<100` のBは直接の片辺として扱う)。
struct SwitchComparisonBands {
    // switchName が式中に一度でも出現したか。falseなら他のフィールドは無視してよい。
    bool referenced = false;
    // switchName が単純な直接比較以外の形で出現したか。trueの場合、thresholds の
    // 内容だけでは式の挙動を正しく近似できない(安全側でフォールバックすること)。
    bool hasUnsupportedReference = false;
    // switchName が直接比較の片辺として現れた箇所の、比較相手の定数値の一覧
    // (演算子の向き・種類は問わない。呼び出し側が閾値そのものとして扱う想定)。
    std::vector<int32_t> thresholds;
};

// expr(EvalSwitchExprと同じ条件式文字列)をパースし、switchName についての
// SwitchComparisonBands を返す。パース自体に失敗した場合は、expr内にswitchNameの
// 文字列が含まれるかどうかだけで安全側に判定する(referenced/hasUnsupportedReferenceを
// 保守的にtrueにする)。
SwitchComparisonBands AnalyzeSwitchComparisons(const std::string& expr, const std::string& switchName);

// expr(EvalSwitchExprと同じ条件式文字列)が switchName を(比較形式・演算文脈を
// 問わず)一度でも参照しているかどうか。AnalyzeSwitchComparisonsと異なり比較の
// 妥当性は判定しない軽量版で、「この式はswitchNameに依存するか」だけを知りたい
// 呼び出し側(例: PluginSession::IsFullyStatic()の除外判定)向け。パース自体に
// 失敗した場合は expr内にswitchNameの文字列が含まれるかどうかで安全側に倒す。
bool ExprReferencesSwitch(const std::string& expr, const std::string& switchName);

} // namespace rs::legacy_pi
