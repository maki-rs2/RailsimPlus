//
// Diagnostics.h - パーサ診断（行・列・原因）
//
#pragma once

#include <string>
#include <vector>

namespace rs::legacy_pi {

enum class Severity { Warning, Error };

struct Diagnostic {
    Severity    severity = Severity::Error;
    int         line = 0;   // 1-based
    int         column = 0; // 1-based
    std::string message;    // UTF-8
};

class Diagnostics {
public:
    void Warn(int line, int col, std::string msg) {
        items_.push_back({Severity::Warning, line, col, std::move(msg)});
    }
    void Error(int line, int col, std::string msg) {
        items_.push_back({Severity::Error, line, col, std::move(msg)});
        ++errorCount_;
    }
    const std::vector<Diagnostic>& Items() const { return items_; }
    bool HasErrors() const { return errorCount_ > 0; }
    int  ErrorCount() const { return errorCount_; }

private:
    std::vector<Diagnostic> items_;
    int                     errorCount_ = 0;
};

} // namespace rs::legacy_pi
