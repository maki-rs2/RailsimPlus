//
// CompatReport.h - 手持ち資産の一括パース結果(compat_test)の集計と合否判定
//
// 資産1件ごとの結果(CompatRecord)を種別ごとに数え、未対応の項目を集め、
// 互換の受け入れ基準を満たすかを判定する純関数群。ファイル走査とパース呼び出しは
// CompatScan 側が行い、ここは入出力を持たない。
//
#pragma once

#include <map>
#include <string>
#include <vector>

namespace rs::app {

inline constexpr const char* kCompatKindRs2 = "rs2";

// 資産1件のパース結果。warnings は読み飛ばした項目・欠落ファイルなど、
// パース自体は成功した上での指摘(自由文。集計時に項目名へ正規化する)。
struct CompatRecord {
    std::string              kind;  // "Train" / "Rail" / ... / kCompatKindRs2
    std::string              path;  // 表示用(UTF-8)
    bool                     parsed = false;
    std::vector<std::string> warnings;
};

enum class CompatOutcome { Ok, Warn, Fail };

CompatOutcome ClassifyCompatRecord(const CompatRecord& record);

// 警告文を未対応項目の名前へ正規化する。先頭の "[..]" タグ・行番号・
// ": " 以降の個別の値(パスなど)を落とし、数字の並びを N に置き換えて、同じ種類の指摘を同じ名前に寄せる。
// "未知の" で始まる文は、ブロック名そのものが項目なので全文を残す。
std::string NormalizeUnsupportedItem(const std::string& warning);

struct CompatKindStats {
    int                        ok = 0;
    int                        warn = 0;
    int                        fail = 0;
    std::map<std::string, int> unsupported;  // 正規化した項目名 -> 件数
    std::vector<std::string>   failedPaths;

    int Total() const { return ok + warn + fail; }
    // 警告つきを含む「パースできた」割合(総数0なら0)
    double SuccessRate() const;
};

struct CompatSummary {
    std::map<std::string, CompatKindStats> byKind;
};

CompatSummary AggregateCompat(const std::vector<CompatRecord>& records);

// REQUIREMENTS §3 の基準1行ぶん。
struct CompatCriterion {
    const char* kind;
    double      minSuccessRate;  // 0..1
};

// 基準のある種別の表(§3.1 は Train と同基準の主要種別、§3.3 は rs2)
const std::vector<CompatCriterion>& CompatCriteria();

enum class CompatVerdict { Pass, Fail, NoData };

struct CompatJudgement {
    std::string   kind;
    double        required = 0;
    double        actual = 0;
    int           total = 0;
    CompatVerdict verdict = CompatVerdict::NoData;
};

// 基準のある種別ごとの判定。資産が0件の種別は NoData(不合格にはしない)。
std::vector<CompatJudgement> JudgeCompat(const CompatSummary& summary);

// 判定に Fail を含まないか(NoData は問わない)
bool CompatPassed(const std::vector<CompatJudgement>& judgements);

// 種別ごとの件数表・未対応項目・失敗一覧・合否を人が読む文字列にする。
std::string FormatCompatReport(const CompatSummary& summary,
                               const std::vector<CompatJudgement>& judgements);

}  // namespace rs::app
