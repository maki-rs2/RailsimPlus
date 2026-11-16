#include "PluginCatalogScanner.h"

#include <algorithm>
#include <filesystem>
#include <stop_token>

#include <spdlog/spdlog.h>

#include "../../platform/text/Encoding.h"
#include "../../plugins/load/PluginHeaderRead.h"

namespace rs::app::plugin_browser {

namespace {

namespace fs = std::filesystem;

// ReadHeadersの1件で調べるフォルダ数。1件ずつだと出口のロックが多すぎるためまとめる。
constexpr std::size_t kFoldersPerRequest = 64;
// 優先して走査する種別と、それ以外の優先度(小さいほど先)。
constexpr int kPriorityHigh = 0;
constexpr int kPriorityNormal = 1;

ScanResponse ListKindDir(const ScanRequest& request, std::stop_token stopToken) {
    ScanResponse response;
    response.type = request.type;
    response.serial = request.serial;
    const fs::path dir = fs::path(rs::platform_text::Utf8ToWide(request.rootUtf8)) /
                         rs::platform_text::Utf8ToWide(GetBrowserKind(request.kind).folderName);
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) return response; // 種別フォルダが無いルートは空として扱う
    for (const fs::directory_entry& item : it) {
        if (stopToken.stop_requested()) break;
        std::error_code itemEc;
        if (!item.is_directory(itemEc)) continue;
        response.folderNames.push_back(rs::platform_text::WideToUtf8(item.path().filename().wstring()));
    }
    std::sort(response.folderNames.begin(), response.folderNames.end());
    return response;
}

ScanResponse ReadHeaders(const ScanRequest& request, std::stop_token stopToken) {
    ScanResponse response;
    response.type = request.type;
    response.serial = request.serial;
    response.results.reserve(request.folders.size());
    for (const ScanRequest::Folder& folder : request.folders) {
        if (stopToken.stop_requested()) break;
        PluginScanResult result;
        result.generation = request.generation;
        result.rootIndex = request.rootIndex;
        result.kind = request.kind;
        result.relativeKey = folder.relativeKey;
        result.folderUtf8 = folder.folderUtf8;
        result.header = rs::legacy_plugin::ReadPluginHeader(rs::platform_text::Utf8ToWide(folder.folderUtf8));
        response.results.push_back(std::move(result));
    }
    return response;
}

// ワーカースレッドで動く。受付だけを見て、ほかの状態には触らない。
ScanResponse ProcessRequest(const ScanRequest& request, std::stop_token stopToken) {
    if (request.type == ScanRequest::Type::ListKindDir) return ListKindDir(request, stopToken);
    return ReadHeaders(request, stopToken);
}

} // namespace

PluginCatalogScanner::PluginCatalogScanner() : worker_(&ProcessRequest) {}

int PluginCatalogScanner::PriorityOf(BrowserKind kind) const {
    return std::find(priorityKinds_.begin(), priorityKinds_.end(), kind) != priorityKinds_.end() ? kPriorityHigh
                                                                                                 : kPriorityNormal;
}

void PluginCatalogScanner::Post(ScanRequest request) {
    request.serial = nextSerial_++;
    const int priority = PriorityOf(request.kind);
    outstanding_[request.serial] = request;
    worker_.Post(std::move(request), priority);
}

void PluginCatalogScanner::Start(const std::vector<std::string>& rootsUtf8, std::uint64_t generation,
                                 const std::vector<BrowserKind>& priorityKinds) {
    worker_.ClearPending();
    outstanding_.clear(); // 処理中の1件の結果は、突き合わせる受付が無いので捨てられる
    unmerged_.clear();
    priorityKinds_ = priorityKinds;
    generation_ = generation;
    rootCount_ = rootsUtf8.size();
    wasScanning_ = true;
    startTime_ = std::chrono::steady_clock::now();

    // 同じ優先度の中ではルート順・種別表の順に処理されるよう、その順にPostする。
    for (std::size_t rootIndex = 0; rootIndex < rootsUtf8.size(); ++rootIndex) {
        for (const BrowserKind kind : GetScanKinds()) {
            ScanRequest request;
            request.type = ScanRequest::Type::ListKindDir;
            request.generation = generation;
            request.rootIndex = rootIndex;
            request.rootUtf8 = rootsUtf8[rootIndex];
            request.kind = kind;
            Post(std::move(request));
        }
    }
}

void PluginCatalogScanner::SetPriorityKinds(const std::vector<BrowserKind>& priorityKinds) {
    priorityKinds_ = priorityKinds;
    // 受付の写しを出し直す。処理中の1件も写しに残っているので二重に積まれるが、
    // 先の結果を受け取った時点で写しから消えるため、後の結果は突き合わせられず捨てられる。
    worker_.ClearPending();
    for (const std::pair<const std::uint64_t, ScanRequest>& item : outstanding_) {
        worker_.Post(item.second, PriorityOf(item.second.kind));
    }
}

void PluginCatalogScanner::HandleListResponse(const ScanRequest& request, const ScanResponse& response) {
    const std::string kindFolder = GetBrowserKind(request.kind).folderName;
    const fs::path kindDir = fs::path(rs::platform_text::Utf8ToWide(request.rootUtf8)) /
                             rs::platform_text::Utf8ToWide(kindFolder);
    for (std::size_t begin = 0; begin < response.folderNames.size(); begin += kFoldersPerRequest) {
        const std::size_t end = std::min(begin + kFoldersPerRequest, response.folderNames.size());
        ScanRequest next;
        next.type = ScanRequest::Type::ReadHeaders;
        next.generation = request.generation;
        next.rootIndex = request.rootIndex;
        next.kind = request.kind;
        for (std::size_t i = begin; i < end; ++i) {
            const std::string& name = response.folderNames[i];
            ScanRequest::Folder folder;
            folder.folderUtf8 = rs::platform_text::WideToUtf8((kindDir / rs::platform_text::Utf8ToWide(name)).wstring());
            folder.relativeKey = kindFolder + "/" + name;
            next.folders.push_back(std::move(folder));
        }
        Post(std::move(next));
    }
}

void PluginCatalogScanner::HandleResponse(ScanResponse& response) {
    const std::map<std::uint64_t, ScanRequest>::iterator found = outstanding_.find(response.serial);
    if (found == outstanding_.end()) return; // やり直し前の結果、または二重に積んだ片方
    const ScanRequest request = std::move(found->second);
    outstanding_.erase(found);
    if (request.generation != generation_) return;
    if (response.type == ScanRequest::Type::ListKindDir) {
        HandleListResponse(request, response);
    } else {
        for (PluginScanResult& result : response.results) unmerged_.push_back(std::move(result));
    }
}

void PluginCatalogScanner::Tick(PluginCatalog& catalog, const FrameBudget& budget) {
    // 予算の中で、Mergeを先にし、溜まりが無いときだけ出口を1件ずつ受け取る(持ち越しを膨らませない)。
    // 予算を超えていても、1フレームに最低1件は進める。
    bool progressed = false;
    while (!progressed || !budget.Exceeded()) {
        if (!unmerged_.empty()) {
            catalog.Merge(unmerged_.front());
            unmerged_.pop_front();
        } else {
            std::vector<ScanResponse> responses = worker_.Drain(1);
            if (responses.empty()) break;
            HandleResponse(responses.front());
        }
        progressed = true;
    }

    if (wasScanning_ && !Scanning()) {
        wasScanning_ = false;
        const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - startTime_)
                                        .count();
        spdlog::info("[catalog] scanned {} entries in {} ms (roots={})", catalog.Entries().size(), elapsedMs,
                     rootCount_);
    }
}

bool PluginCatalogScanner::Scanning() const {
    return !outstanding_.empty() || !unmerged_.empty();
}

} // namespace rs::app::plugin_browser
