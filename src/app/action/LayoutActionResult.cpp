#include "LayoutActionResult.h"

#include <iterator>

#include <magic_enum/magic_enum.hpp>

#include "../workspace/CatalogCheck.h"

namespace rs::app {
namespace {

struct StatusTraits {
    LayoutActionStatus status;
    const char* messageKey;
    ErrorSeverity severity;
};

constexpr StatusTraits kStatusTraits[] = {
    {LayoutActionStatus::Ok, "", ErrorSeverity::Recoverable},
    {LayoutActionStatus::FileNotFound, "action.layout.file_not_found", ErrorSeverity::Recoverable},
    {LayoutActionStatus::FileWriteFailed, "action.layout.file_write_failed", ErrorSeverity::Recoverable},
    {LayoutActionStatus::InvalidFormat, "action.layout.invalid_format", ErrorSeverity::Recoverable},
    {LayoutActionStatus::ImportFailed, "action.layout.import_failed", ErrorSeverity::Recoverable},
    {LayoutActionStatus::ClassifierMissing, "action.layout.classifier_missing", ErrorSeverity::Critical},
    {LayoutActionStatus::Unexpected, "action.layout.unexpected", ErrorSeverity::Critical},
    {LayoutActionStatus::Busy, "action.layout.busy", ErrorSeverity::Recoverable},
};

static_assert(std::size(kStatusTraits) == magic_enum::enum_count<LayoutActionStatus>());
static_assert(workspace::IsCatalogInEnumOrder(kStatusTraits, &StatusTraits::status));

} // namespace

ErrorSeverity SeverityOf(LayoutActionStatus status) {
    return kStatusTraits[workspace::EnumIndex(status)].severity;
}

LayoutActionResult LayoutActionResult::Failure(LayoutActionStatus status, const std::string& detail,
                                               const std::string& diagnostic) {
    LayoutActionResult result;
    result.status = status;
    result.messageKey = kStatusTraits[workspace::EnumIndex(status)].messageKey;
    result.detail = detail;
    result.diagnostic = diagnostic;
    return result;
}

} // namespace rs::app
