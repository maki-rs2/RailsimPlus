#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ActionError.h"
#include "../plugin_session/Rs2SceneApplier.h"

namespace rs::app {

// 識別子はログと不具合報告で使うため、表示する言語によらず同じ値を保つ。
enum class LayoutActionStatus {
    Ok,
    FileNotFound,
    FileWriteFailed,
    InvalidFormat,
    ImportFailed,
    ClassifierMissing,
    Unexpected,
    Busy,
};

ErrorSeverity SeverityOf(LayoutActionStatus status);

struct LayoutActionResult {
    LayoutActionStatus status = LayoutActionStatus::Ok;
    std::string messageKey;
    // detailはUIへ渡すパスや例外の素データ。diagnosticは英語の開発ログ用。
    std::string detail;
    std::string diagnostic;
    std::string savedPathUtf8;
    std::vector<std::string> warnings;
    size_t nodeCount = 0;
    size_t edgeCount = 0;
    size_t placementCount = 0;
    size_t sceneCount = 0;
    rs::world::Guid guid = rs::world::NilGuid();
    size_t pluginFoldersLoaded = 0;
    size_t pluginFoldersFailed = 0;
    size_t placementsAdded = 0;
    std::vector<ImportedWireSpan> wireSpans;

    bool Ok() const { return status == LayoutActionStatus::Ok; }
    static LayoutActionResult Failure(LayoutActionStatus status, const std::string& detail,
                                      const std::string& diagnostic = {});
};

} // namespace rs::app
