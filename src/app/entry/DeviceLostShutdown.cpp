#include "DeviceLostShutdown.h"

#include <exception>

#include <spdlog/spdlog.h>

namespace rs::app {

DeviceLostOutcome RunDeviceLostShutdown(const DeviceLostActions& actions) {
    DeviceLostOutcome outcome;
    if (actions.logReason) actions.logReason();
    if (actions.shouldSaveBackup && actions.shouldSaveBackup() && actions.saveBackup) {
        outcome.backupAttempted = true;
        try {
            outcome.backupSaved = actions.saveBackup();
        } catch (const std::exception& exception) {
            spdlog::error("[device_lost] バックアップの保存で例外: {}", exception.what());
        }
    }
    if (actions.showError) actions.showError(outcome.backupAttempted, outcome.backupSaved);
    if (actions.quit) actions.quit();
    return outcome;
}

} // namespace rs::app
