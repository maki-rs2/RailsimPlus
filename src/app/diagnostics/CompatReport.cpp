//
// CompatReport.cpp - CompatReport.h の実装
//
#include "CompatReport.h"

#include <algorithm>
#include <cstdio>

namespace rs::app {

namespace {

constexpr double kPluginSuccessRate = 0.95;  // REQUIREMENTS §3.1
constexpr double kLayoutSuccessRate = 1.0;   // REQUIREMENTS §3.3
constexpr size_t kMaxListedFailures = 20;

template <typename... Args>
std::string Fmt(const char* format, Args... args) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), format, args...);
    return buf;
}

}  // namespace

CompatOutcome ClassifyCompatRecord(const CompatRecord& record) {
    if (!record.parsed) return CompatOutcome::Fail;
    return record.warnings.empty() ? CompatOutcome::Ok : CompatOutcome::Warn;
}

std::string NormalizeUnsupportedItem(const std::string& warning) {
    std::string s = warning;
    // 先頭の "[plugin][warn] " のようなタグを落とす
    for (;;) {
        size_t start = s.find_first_not_of(' ');
        s.erase(0, start == std::string::npos ? s.size() : start);
        if (s.empty() || s[0] != '[') break;
        size_t close = s.find(']');
        if (close == std::string::npos) break;
        s.erase(0, close + 1);
    }
    // "12: message" の行番号
    size_t digits = 0;
    while (digits < s.size() && s[digits] >= '0' && s[digits] <= '9') ++digits;
    if (digits > 0 && s.compare(digits, 2, ": ") == 0) s.erase(0, digits + 2);

    static const std::string kUnknownPrefix = "未知の";
    if (s.compare(0, kUnknownPrefix.size(), kUnknownPrefix) == 0) return s;
    size_t colon = s.find(": ");
    if (colon != std::string::npos) s.erase(colon);
    size_t end = s.find_last_not_of(' ');
    s.erase(end == std::string::npos ? 0 : end + 1);
    // "matID 3 out of range" のような添字違いを同じ項目に寄せる
    std::string collapsed;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] >= '0' && s[i] <= '9') {
            while (i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') ++i;
            collapsed += 'N';
        } else {
            collapsed += s[i];
        }
    }
    return collapsed;
}

double CompatKindStats::SuccessRate() const {
    int total = Total();
    return total == 0 ? 0.0 : static_cast<double>(ok + warn) / total;
}

CompatSummary AggregateCompat(const std::vector<CompatRecord>& records) {
    CompatSummary summary;
    for (const CompatRecord& r : records) {
        CompatKindStats& st = summary.byKind[r.kind];
        switch (ClassifyCompatRecord(r)) {
            case CompatOutcome::Ok: ++st.ok; break;
            case CompatOutcome::Warn: ++st.warn; break;
            case CompatOutcome::Fail:
                ++st.fail;
                st.failedPaths.push_back(r.path);
                break;
        }
        for (const std::string& w : r.warnings) ++st.unsupported[NormalizeUnsupportedItem(w)];
    }
    return summary;
}

const std::vector<CompatCriterion>& CompatCriteria() {
    static const std::vector<CompatCriterion> kTable = {
        {"Train", kPluginSuccessRate},   {"Rail", kPluginSuccessRate},
        {"Tie", kPluginSuccessRate},     {"Girder", kPluginSuccessRate},
        {"Pier", kPluginSuccessRate},    {"Pole", kPluginSuccessRate},
        {"Line", kPluginSuccessRate},    {"Struct", kPluginSuccessRate},
        {"Station", kPluginSuccessRate}, {"Surface", kPluginSuccessRate},
        {kCompatKindRs2, kLayoutSuccessRate},
    };
    return kTable;
}

std::vector<CompatJudgement> JudgeCompat(const CompatSummary& summary) {
    std::vector<CompatJudgement> out;
    for (const CompatCriterion& c : CompatCriteria()) {
        CompatJudgement j;
        j.kind = c.kind;
        j.required = c.minSuccessRate;
        auto it = summary.byKind.find(j.kind);
        if (it != summary.byKind.end() && it->second.Total() > 0) {
            j.total = it->second.Total();
            j.actual = it->second.SuccessRate();
            j.verdict = j.actual >= j.required ? CompatVerdict::Pass : CompatVerdict::Fail;
        }
        out.push_back(j);
    }
    return out;
}

bool CompatPassed(const std::vector<CompatJudgement>& judgements) {
    return std::none_of(judgements.begin(), judgements.end(), [](const CompatJudgement& j) {
        return j.verdict == CompatVerdict::Fail;
    });
}

std::string FormatCompatReport(const CompatSummary& summary,
                               const std::vector<CompatJudgement>& judgements) {
    std::string out = "=== compat_test ===\n";
    CompatKindStats all;
    for (const auto& [kind, st] : summary.byKind) {
        out += Fmt("%-8s: total=%d OK=%d warn=%d fail=%d (parse success %.1f%%)\n", kind.c_str(),
                   st.Total(), st.ok, st.warn, st.fail, st.SuccessRate() * 100.0);
        all.ok += st.ok;
        all.warn += st.warn;
        all.fail += st.fail;
    }
    out += Fmt("TOTAL   : total=%d OK=%d warn=%d fail=%d\n", all.Total(), all.ok, all.warn,
               all.fail);

    for (const auto& [kind, st] : summary.byKind) {
        if (!st.unsupported.empty()) {
            out += Fmt("--- unsupported items [%s] ---\n", kind.c_str());
            std::vector<std::pair<std::string, int>> items(st.unsupported.begin(),
                                                           st.unsupported.end());
            std::stable_sort(items.begin(), items.end(),
                             [](const auto& a, const auto& b) { return a.second > b.second; });
            for (const auto& [name, count] : items)
                out += Fmt("  %6d  %s\n", count, name.c_str());
        }
        if (!st.failedPaths.empty()) {
            out += Fmt("--- failed [%s] ---\n", kind.c_str());
            for (size_t i = 0; i < st.failedPaths.size() && i < kMaxListedFailures; ++i)
                out += "  " + st.failedPaths[i] + "\n";
            if (st.failedPaths.size() > kMaxListedFailures)
                out += Fmt("  ... and %zu more\n", st.failedPaths.size() - kMaxListedFailures);
        }
    }

    out += "--- REQUIREMENTS section 3 judgement ---\n";
    for (const CompatJudgement& j : judgements) {
        if (j.verdict == CompatVerdict::NoData) {
            out += Fmt("  %-8s: n/a (no assets)\n", j.kind.c_str());
        } else {
            out += Fmt("  %-8s: %s  %.1f%% (need %.0f%%, n=%d)\n", j.kind.c_str(),
                       j.verdict == CompatVerdict::Pass ? "PASS" : "FAIL", j.actual * 100.0,
                       j.required * 100.0, j.total);
        }
    }
    out += CompatPassed(judgements) ? "RESULT: PASS\n" : "RESULT: FAIL\n";
    return out;
}

}  // namespace rs::app
