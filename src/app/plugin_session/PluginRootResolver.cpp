#include "PluginRootResolver.h"

#include <filesystem>
#include <set>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "../../platform/text/Encoding.h"

namespace rs::app {
namespace {

std::filesystem::path Utf8Path(const std::string& value) {
    return std::filesystem::path(rs::platform_text::Utf8ToWide(value));
}

std::string Utf8String(const std::filesystem::path& value) {
    return rs::platform_text::WideToUtf8(value.generic_wstring());
}

bool IsInsideRoot(const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute()) return false;
    for (const std::filesystem::path& part : relative) {
        if (part == "..") return false;
    }
    return true;
}

// 1つのプラグイン参照文字列を変換する。同じプラグインは大量の配置個体から参照されるため、
// ルート外の警告は参照文字列ごとに1回だけ出す。
void TransformOneReference(nlohmann::json& slot, const std::vector<std::string>& rootsUtf8,
                           bool resolving, std::set<std::string>& warnedUtf8,
                           const PluginPathCanonicalizer& canonicalize) {
    if (!slot.is_string()) return;
    const std::string original = slot.get<std::string>();
    // 空参照(プラグイン無しの架線専用アンカー等)はそのまま保存し、ルート外警告の対象外。
    if (original.empty()) return;
    const std::string transformed = resolving
        ? ResolveAgainstPluginRoots(rootsUtf8, original, warnedUtf8)
        : RelativizeToPluginRoot(rootsUtf8, original);
    if (!transformed.empty()) {
        slot = (resolving && canonicalize) ? canonicalize(transformed) : transformed;
        return;
    }
    if (!resolving && warnedUtf8.insert(original).second) {
        spdlog::warn("Plugin '{}' is outside registered plugin roots; saving absolute path.", original);
    }
}

void TransformPluginReferences(nlohmann::json& value, const std::vector<std::string>& rootsUtf8,
                               bool resolving, std::set<std::string>& warnedUtf8,
                               const PluginPathCanonicalizer& canonicalize) {
    if (value.is_object()) {
        for (nlohmann::json::iterator item = value.begin(); item != value.end(); ++item) {
            if (item.key() == "plugin" && item.value().is_string()) {
                TransformOneReference(item.value(), rootsUtf8, resolving, warnedUtf8, canonicalize);
            } else if ((item.key() == "plugins" || item.key() == "defaultPlugins") &&
                       item.value().is_array()) {
                for (nlohmann::json& folder : item.value()) {
                    TransformOneReference(folder, rootsUtf8, resolving, warnedUtf8, canonicalize);
                }
            } else {
                TransformPluginReferences(item.value(), rootsUtf8, resolving, warnedUtf8, canonicalize);
            }
        }
    } else if (value.is_array()) {
        for (nlohmann::json& item : value) {
            TransformPluginReferences(item, rootsUtf8, resolving, warnedUtf8, canonicalize);
        }
    }
}

}  // namespace

std::string RelativizeToPluginRoot(const std::vector<std::string>& rootsUtf8,
                                   const std::string& absoluteUtf8) {
    const std::filesystem::path absolute = Utf8Path(absoluteUtf8);
    for (const std::string& rootUtf8 : rootsUtf8) {
        const std::filesystem::path relative = absolute.lexically_relative(Utf8Path(rootUtf8));
        if (IsInsideRoot(relative)) return Utf8String(relative);
    }
    return {};
}

std::string ResolveAgainstPluginRoots(const std::vector<std::string>& rootsUtf8,
                                      const std::string& storedUtf8,
                                      std::set<std::string>& warnedConflictsUtf8) {
    const std::filesystem::path stored = Utf8Path(storedUtf8);
    if (stored.is_absolute() || rootsUtf8.empty()) return storedUtf8;
    for (size_t index = 0; index < rootsUtf8.size(); ++index) {
        const std::filesystem::path candidate = Utf8Path(rootsUtf8[index]) / stored;
        if (!std::filesystem::exists(candidate)) continue;
        // 同じ相対パスが後続のrootにも存在する場合は、先頭側(登録順で先)を採用しつつ
        // 1回だけ警告する(仕様確定: 優先順位=登録順、保存側にroot情報は残さない)。
        if (warnedConflictsUtf8.insert(storedUtf8).second) {
            for (size_t laterIndex = index + 1; laterIndex < rootsUtf8.size(); ++laterIndex) {
                if (std::filesystem::exists(Utf8Path(rootsUtf8[laterIndex]) / stored)) {
                    spdlog::warn(
                        "Plugin '{}' exists under multiple plugin roots; using '{}' (registration order).",
                        storedUtf8, rootsUtf8[index]);
                    break;
                }
            }
        }
        return Utf8String(candidate);
    }
    std::string joined;
    for (size_t index = 0; index < rootsUtf8.size(); ++index) {
        if (index != 0) joined += ", ";
        joined += rootsUtf8[index];
    }
    spdlog::warn("Plugin '{}' was not found under any plugin root candidate: [{}]", storedUtf8, joined);
    return Utf8String(Utf8Path(rootsUtf8.front()) / stored);
}

std::vector<std::string> BuildPluginRootCandidates(const std::string& derivedRootUtf8,
                                                   const std::vector<std::string>& configuredRootsUtf8,
                                                   const std::string& executableDirectoryUtf8) {
    std::vector<std::string> candidates;
    const auto appendCandidate = [&candidates](const std::string& candidateUtf8) {
        if (candidateUtf8.empty()) return;
        const std::string normalized = Utf8String(Utf8Path(candidateUtf8));
        if (normalized.empty()) return;
        for (const std::string& candidate : candidates) {
            if (candidate == normalized) return;
        }
        candidates.push_back(normalized);
    };
    appendCandidate(derivedRootUtf8);
    for (const std::string& rootUtf8 : configuredRootsUtf8) appendCandidate(rootUtf8);
    appendCandidate(executableDirectoryUtf8);
    return candidates;
}

void RelativizeLayoutPluginReferences(nlohmann::json& document,
                                     const std::vector<std::string>& rootsUtf8) {
    std::set<std::string> warnedUtf8;
    TransformPluginReferences(document, rootsUtf8, false, warnedUtf8, {});
}

void ResolveLayoutPluginReferences(nlohmann::json& document,
                                   const std::vector<std::string>& rootsUtf8,
                                   const PluginPathCanonicalizer& canonicalize) {
    std::set<std::string> warnedUtf8;
    TransformPluginReferences(document, rootsUtf8, true, warnedUtf8, canonicalize);
}

}  // namespace rs::app
