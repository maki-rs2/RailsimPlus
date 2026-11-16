//
// PluginCatalogScanner.h - プラグインルートを走査してカタログへ結果を流す(ワーカー1本)
//
// 受付は2種類で、
//   ListKindDir: <ルート>/<種別フォルダ> 直下のフォルダ名を列挙する
//   ReadHeaders: フォルダ最大64件のヘッダを読む
// メインスレッドはTickで出口を受け取り、列挙の結果を次のReadHeadersへ分け、
// ヘッダの結果をカタログへMergeする。メインスレッド専用(ワーカーはカタログに触らない)。
//
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "../../platform/thread/WorkerThread.h"
#include "BrowserFrameBudget.h"
#include "BrowserKindCatalog.h"
#include "PluginCatalog.h"

namespace rs::app::plugin_browser {

// 受付の1件。種類はtypeで区別する。
struct ScanRequest {
    enum class Type { ListKindDir, ReadHeaders };

    // ReadHeadersで調べるフォルダ1つ。
    struct Folder {
        std::string folderUtf8;   // 絶対パス
        std::string relativeKey;  // "<種別フォルダ>/<フォルダ名>"
    };

    Type type = Type::ListKindDir;
    std::uint64_t serial = 0;     // 受付ごとの連番(結果と突き合わせる)
    std::uint64_t generation = 0;
    std::size_t rootIndex = 0;
    std::string rootUtf8;         // ListKindDirで使う
    BrowserKind kind = BrowserKind::Rail;
    std::vector<Folder> folders;  // ReadHeadersで使う
};

// 受付1件に対する結果。
struct ScanResponse {
    ScanRequest::Type type = ScanRequest::Type::ListKindDir;
    std::uint64_t serial = 0;
    std::vector<std::string> folderNames;      // ListKindDir: 見つかったサブフォルダ名
    std::vector<PluginScanResult> results;     // ReadHeaders: 1フォルダ1件
};

class PluginCatalogScanner {
public:
    PluginCatalogScanner();

    // 受付を捨てて走査をやり直す。priorityKindsに入る種別を先に走査する。
    void Start(const std::vector<std::string>& rootsUtf8, std::uint64_t generation,
               const std::vector<BrowserKind>& priorityKinds);

    // 優先する種別を付け直す。まだ始めていない受付は並べ直される。
    void SetPriorityKinds(const std::vector<BrowserKind>& priorityKinds);

    // 出口を受け取り、列挙の結果は次の受付へ分け、ヘッダの結果はcatalogへMergeする。
    // budgetの時間内で続ける(超えても最低1件は進める)。
    void Tick(PluginCatalog& catalog, const FrameBudget& budget);

    // 未処理・処理中・未マージの結果のどれかがあればtrue。
    bool Scanning() const;

private:
    int PriorityOf(BrowserKind kind) const;
    void Post(ScanRequest request);
    void HandleResponse(ScanResponse& response);
    void HandleListResponse(const ScanRequest& request, const ScanResponse& response);

    // 出口を受け取る前の受付の写し(serial -> 受付)。優先度の付け直しと、結果の突き合わせに使う。
    std::map<std::uint64_t, ScanRequest> outstanding_;
    std::deque<PluginScanResult> unmerged_; // 受け取ったが、まだMergeしていない結果
    std::vector<BrowserKind> priorityKinds_;
    std::uint64_t generation_ = 0;
    std::uint64_t nextSerial_ = 1;
    std::size_t rootCount_ = 0;
    bool wasScanning_ = false;
    std::chrono::steady_clock::time_point startTime_;

    // ワーカーは受付だけで動き、他のメンバに触らない。最後に置いて、最初に止める。
    rs::platform_thread::WorkerThread<ScanRequest, ScanResponse> worker_;
};

} // namespace rs::app::plugin_browser
