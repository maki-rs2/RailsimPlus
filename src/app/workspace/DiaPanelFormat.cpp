#include "DiaPanelFormat.h"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace rs::app::workspace {
namespace {

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return text;
}

bool ParseInt(std::string_view text, int& out) {
    if (text.empty() || text.size() > 6) return false;
    const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

} // namespace

std::string DiaRunInfoFieldLabel(rs::world::RunInfoChange::Field field) {
    for (const DiaRunInfoFieldName& name : kDiaRunInfoFieldNames) {
        if (name.key == static_cast<int>(field)) return name.label;
    }
    return {};
}

const char* DiaActionLabel(rs::world::DiaAction action) {
    switch (action) {
    case rs::world::DiaAction::Stop: return "停車";
    case rs::world::DiaAction::Reverse: return "折返";
    case rs::world::DiaAction::Pass: return "通過";
    }
    return "停車";
}

std::string FormatDiaTime(const rs::world::DiaTimeOfDay& time) {
    char buffer[32];
    if (time.second == 0) {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", time.hour, time.minute);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", time.hour, time.minute, time.second);
    }
    return buffer;
}

bool ParseDiaTime(std::string_view text, rs::world::DiaTimeOfDay& out) {
    text = Trim(text);
    const std::size_t firstColon = text.find(':');
    if (firstColon == std::string_view::npos) return false;
    const std::size_t secondColon = text.find(':', firstColon + 1);
    rs::world::DiaTimeOfDay parsed;
    const std::string_view hourText = text.substr(0, firstColon);
    const std::string_view minuteText = secondColon == std::string_view::npos
        ? text.substr(firstColon + 1) : text.substr(firstColon + 1, secondColon - firstColon - 1);
    if (!ParseInt(hourText, parsed.hour) || !ParseInt(minuteText, parsed.minute)) return false;
    if (secondColon != std::string_view::npos && !ParseInt(text.substr(secondColon + 1), parsed.second)) return false;
    if (parsed.hour < 0 || parsed.minute < 0 || parsed.minute > 59 || parsed.second < 0 || parsed.second > 59) return false;
    out = parsed;
    return true;
}

std::string FormatDiaSeconds(double seconds) {
    char buffer[32];
    if (std::fabs(seconds - std::round(seconds)) < 1e-9) {
        std::snprintf(buffer, sizeof(buffer), "%.0f", seconds);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f", seconds);
    }
    return buffer;
}

bool ParseDiaSeconds(std::string_view text, double& out) {
    text = Trim(text);
    if (text.empty() || text.size() > 12) return false;
    double value = 0.0;
    const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size() || !std::isfinite(value) || value < 0.0) return false;
    out = value;
    return true;
}

std::string FormatPointHand(int hand, bool random) {
    if (random) return "ランダム";
    return hand == 0 ? "左" : "右";
}

bool ParsePointHand(std::string_view text, int& hand, bool& random) {
    if (text == "左") {
        hand = 0;
        random = false;
    } else if (text == "右") {
        hand = 1;
        random = false;
    } else if (text == "ランダム") {
        random = true;
    } else {
        return false;
    }
    return true;
}

} // namespace rs::app::workspace
