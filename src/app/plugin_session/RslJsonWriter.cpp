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

// 改行・インデント無しの1行表現。幅予算の判定と、実際に1行化する際の出力の両方に使う。
std::string DumpInline(const nlohmann::json& value) {
    if (value.is_array()) {
        std::string output = "[";
        for (nlohmann::json::const_iterator it = value.cbegin(); it != value.cend(); ++it) {
            if (it != value.cbegin()) {
                output.append(", ");
            }
            output.append(DumpInline(*it));
        }
        output.push_back(']');
        return output;
    }
    if (value.is_object()) {
        std::string output = "{";
        for (nlohmann::json::const_iterator it = value.cbegin(); it != value.cend(); ++it) {
            if (it != value.cbegin()) {
                output.append(", ");
            }
            output.append(nlohmann::json(it.key()).dump());
            output.append(": ");
            output.append(DumpInline(it.value()));
        }
        output.push_back('}');
        return output;
    }
    return value.dump();
}

bool FitsWidthBudget(const std::string& inlineText, int depth) {
    return static_cast<int>(inlineText.size()) + depth * kIndentWidth <= kMaxLineWidth;
}

void AppendJson(const nlohmann::json& value, int depth, std::string& output);

void AppendArray(const nlohmann::json& value, int depth, std::string& output) {
    if (value.empty()) {
        output.append("[]");
        return;
    }

    const std::string inlineText = DumpInline(value);
    if (FitsWidthBudget(inlineText, depth)) {
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

    const std::string inlineText = DumpInline(value);
    if (FitsWidthBudget(inlineText, depth)) {
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
