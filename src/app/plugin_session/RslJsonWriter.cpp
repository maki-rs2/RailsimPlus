#include "RslJsonWriter.h"

#include <string>

#include "RslJsonFieldRounding.h"

namespace rs::app {
namespace {

constexpr int kIndentWidth = 2;
constexpr int kMaxLineWidth = 160;

void AppendIndent(std::string& output, int depth) {
    output.append(static_cast<size_t>(depth * kIndentWidth), ' ');
}

// 改行・インデント無しの1行表現を、文字数が limit に収まる間だけ作る。超えた時点で打ち切って false を返す
// (大きい配列・オブジェクトで、捨てるだけの全体の1行表現を作らないため)。
bool TryDumpInline(const nlohmann::json& value, std::string& output, std::size_t limit) {
    if (value.is_array()) {
        output.push_back('[');
        for (nlohmann::json::const_iterator it = value.cbegin(); it != value.cend(); ++it) {
            if (it != value.cbegin()) {
                output.append(", ");
            }
            if (output.size() > limit || !TryDumpInline(*it, output, limit)) return false;
        }
        output.push_back(']');
        return output.size() <= limit;
    }
    if (value.is_object()) {
        output.push_back('{');
        for (nlohmann::json::const_iterator it = value.cbegin(); it != value.cend(); ++it) {
            if (it != value.cbegin()) {
                output.append(", ");
            }
            output.append(nlohmann::json(it.key()).dump());
            output.append(": ");
            if (output.size() > limit || !TryDumpInline(it.value(), output, limit)) return false;
        }
        output.push_back('}');
        return output.size() <= limit;
    }
    output.append(value.dump());
    return output.size() <= limit;
}

// 深さ depth の行へ1行で収まるなら、その1行表現を output へ作って true を返す。
bool TryDumpInlineAtDepth(const nlohmann::json& value, int depth, std::string& output) {
    const int limit = kMaxLineWidth - depth * kIndentWidth;
    if (limit < 0) return false;
    return TryDumpInline(value, output, static_cast<std::size_t>(limit));
}

void AppendJson(const nlohmann::json& value, int depth, std::string& output);

void AppendArray(const nlohmann::json& value, int depth, std::string& output) {
    if (value.empty()) {
        output.append("[]");
        return;
    }

    std::string inlineText;
    if (TryDumpInlineAtDepth(value, depth, inlineText)) {
        output.append(inlineText);
        return;
    }

    output.append("[\n");
    for (nlohmann::json::const_iterator it = value.cbegin(); it != value.cend(); ++it) {
        AppendIndent(output, depth + 1);
        AppendJson(*it, depth + 1, output);
        if (std::next(it) != value.cend()) {
            output.push_back(',');
        }
        output.push_back('\n');
    }
    AppendIndent(output, depth);
    output.push_back(']');
}

void AppendObject(const nlohmann::json& value, int depth, std::string& output) {
    if (value.empty()) {
        output.append("{}");
        return;
    }

    std::string inlineText;
    if (TryDumpInlineAtDepth(value, depth, inlineText)) {
        output.append(inlineText);
        return;
    }

    output.append("{\n");
    for (nlohmann::json::const_iterator it = value.cbegin(); it != value.cend(); ++it) {
        AppendIndent(output, depth + 1);
        output.append(nlohmann::json(it.key()).dump());
        output.append(": ");
        AppendJson(it.value(), depth + 1, output);
        if (std::next(it) != value.cend()) {
            output.push_back(',');
        }
        output.push_back('\n');
    }
    AppendIndent(output, depth);
    output.push_back('}');
}

void AppendJson(const nlohmann::json& value, int depth, std::string& output) {
    if (value.is_array()) {
        AppendArray(value, depth, output);
        return;
    }
    if (value.is_object()) {
        AppendObject(value, depth, output);
        return;
    }
    output.append(value.dump());
}

}  // namespace

std::string DumpRslJson(const nlohmann::json& value) {
    const nlohmann::json rounded = RoundRslJsonFields(value);
    std::string output;
    AppendJson(rounded, 0, output);
    return output;
}

}  // namespace rs::app
