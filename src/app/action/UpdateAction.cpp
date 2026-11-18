#include "UpdateAction.h"

#include "../update/UpdateChecker.h"
#include "../update/UpdateDownloader.h"

namespace rs::app {

void UpdateAction::Download() {
    if (checker_ == nullptr || checker_->State() != UpdateCheckState::Available) return;
    if (downloader_ == nullptr || !downloader_->CanDownloadInApp(checker_->Release())) {
        checker_->OpenDownloadPage();
        return;
    }
    downloader_->Start(checker_->Release());
}

void UpdateAction::Cancel() {
    if (downloader_ != nullptr) downloader_->Cancel();
}

void UpdateAction::Restart() {
    if (downloader_ == nullptr || downloader_->State() != UpdateDownloadState::Ready) return;
    if (requestRestart_ != nullptr && *requestRestart_) (*requestRestart_)();
}

} // namespace rs::app
