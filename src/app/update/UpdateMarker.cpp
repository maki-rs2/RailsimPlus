#include "UpdateMarker.h"

#include <array>
#include <nlohmann/json.hpp>

#include "../../platform/file/AtomicFileWrite.h"
#include "../../platform/file/FileRead.h"
#include "../../platform/text/Encoding.h"

namespace rs::app {
namespace {
struct NamedState { UpdateMarkerState state; std::string_view name; };
constexpr std::array<NamedState, 3> kStateNames{{
    {UpdateMarkerState::Applying, "applying"}, {UpdateMarkerState::Pending, "pending"},
    {UpdateMarkerState::RolledBack, "rolledBack"}}};

struct NamedKind { UpdateOperationKind kind; std::string_view name; };
constexpr std::array<NamedKind, 3> kKindNames{{
    {UpdateOperationKind::Replace, "replace"}, {UpdateOperationKind::Add, "add"}, {UpdateOperationKind::Delete, "delete"}}};

std::string_view StateName(UpdateMarkerState state) {
    for (const NamedState& entry : kStateNames) {
        if (entry.state == state) return entry.name;
    }
    return {};
}

std::string_view KindName(UpdateOperationKind kind) {
    for (const NamedKind& entry : kKindNames) {
        if (entry.kind == kind) return entry.name;
    }
    return {};
}

bool ParseOperations(const nlohmann::json& list, UpdatePlan& plan) {
    if (!list.is_array()) return false;
    for (const nlohmann::json& item : list) {
        if (!item.is_object()) return false;
        const auto path = item.find("path");
        const auto kind = item.find("kind");
        if (path == item.end() || !path->is_string() || kind == item.end() || !kind->is_string()) return false;
        UpdateOperation operation;
        operation.path = path->get<std::string>();
        // マーカーの内容でインストール先の外を触らないよう、パスを検査する。
        if (!IsSafeRelativePath(operation.path)) return false;
        bool known = false;
        for (const NamedKind& entry : kKindNames) {
            if (entry.name == kind->get<std::string>()) { operation.kind = entry.kind; known = true; }
        }
        if (!known) return false;
        plan.operations.push_back(std::move(operation));
    }
    return true;
}

bool ParseMarker(const nlohmann::json& root, UpdateMarker& marker) {
    if (!root.is_object()) return false;
    const auto format = root.find("formatVersion");
    if (format == root.end() || !format->is_number_integer() || format->get<int>() != kUpdateMarkerFormatVersion) return false;
    const auto from = root.find("fromVersion");
    const auto to = root.find("toVersion");
    const auto attempts = root.find("attempts");
    const auto state = root.find("state");
    const auto plan = root.find("plan");
    if (from == root.end() || !from->is_string() || to == root.end() || !to->is_string() ||
        attempts == root.end() || !attempts->is_number_integer() || attempts->get<int>() < 0 ||
        state == root.end() || !state->is_string() || plan == root.end()) return false;
    bool knownState = false;
    for (const NamedState& entry : kStateNames) {
        if (entry.name == state->get<std::string>()) { marker.state = entry.state; knownState = true; }
    }
    if (!knownState) return false;
    marker.formatVersion = kUpdateMarkerFormatVersion;
    marker.fromVersion = from->get<std::string>();
    marker.toVersion = to->get<std::string>();
    marker.attempts = attempts->get<int>();
    return ParseOperations(*plan, marker.plan);
}
} // namespace

std::filesystem::path UpdateMarkerPath(const std::filesystem::path& installDirectory) {
    return installDirectory / rs::platform_text::Utf8ToWide(std::string(kUpdateMarkerFileName));
}

UpdateMarkerRead ReadUpdateMarker(const std::filesystem::path& markerPath) {
    UpdateMarkerRead result;
    std::error_code error;
    if (!std::filesystem::exists(markerPath, error)) return result;
    result.status = UpdateMarkerReadStatus::Corrupt;
    const std::optional<std::string> text = rs::platform_file::ReadFileText(markerPath);
    if (!text) return result;
    UpdateMarker marker;
    if (!ParseMarker(nlohmann::json::parse(*text, nullptr, false), marker)) return result;
    result.status = UpdateMarkerReadStatus::Ok;
    result.marker = std::move(marker);
    return result;
}

bool WriteUpdateMarker(const std::filesystem::path& markerPath, const UpdateMarker& marker, std::string& outError) {
    nlohmann::ordered_json operations = nlohmann::ordered_json::array();
    for (const UpdateOperation& operation : marker.plan.operations) {
        operations.push_back({{"path", operation.path}, {"kind", KindName(operation.kind)}});
    }
    // formatVersion を先頭に置く(nlohmann::ordered_json で挿入順を保つ)。
    nlohmann::ordered_json root;
    root["formatVersion"] = marker.formatVersion;
    root["fromVersion"] = marker.fromVersion;
    root["toVersion"] = marker.toVersion;
    root["state"] = StateName(marker.state);
    root["attempts"] = marker.attempts;
    root["plan"] = operations;
    return rs::platform_file::WriteFileAtomically(markerPath, root.dump(2), outError);
}

StartupAction DecideStartupAction(const UpdateMarkerRead& read, std::string_view currentVersion) {
    if (read.status == UpdateMarkerReadStatus::Missing) return StartupAction::None;
    if (read.status == UpdateMarkerReadStatus::Corrupt) return StartupAction::CorruptMarker;
    const UpdateMarker& marker = read.marker;
    if (marker.state == UpdateMarkerState::Applying) return StartupAction::RollbackAndRelaunch;
    if (marker.state == UpdateMarkerState::RolledBack) return StartupAction::NotifyRolledBack;
    if (marker.toVersion != currentVersion) return StartupAction::DiscardStaleMarker;
    return marker.attempts == 0 ? StartupAction::ConfirmPendingStart : StartupAction::RollbackAndRelaunch;
}

} // namespace rs::app
