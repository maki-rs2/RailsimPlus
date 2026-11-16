//
// ParsedPluginCache.h - 焼き込み前のプラグイン読込結果を共有するセッションキャッシュ
//
#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../platform/thread/ParallelFor.h"
#include "../plugin_session/PluginSession.h"
#include "StaticInstanceKey.h"

namespace rs::app {

// キーはPluginSource(フォルダ+定義ファイル名)。同じフォルダでも読む定義ファイルが違えば別の結果になる。
class ParsedPluginCache {
public:
    ParsedPluginCache() = default;
    ParsedPluginCache(const ParsedPluginCache&) = delete;
    ParsedPluginCache& operator=(const ParsedPluginCache&) = delete;
    // 実行中ワーカーをjoinする(detach禁止: ワーカーはthis経由で表へ登録するため)。
    ~ParsedPluginCache();

    // sourceの生パース結果をsessionへ読み込む。キャッシュヒットならtrueを返す。
    bool LoadInto(PluginSession& session, const PluginSource& source, std::vector<std::string>& log,
                  float pierHeightOverrideM = -1.0f);

    // sourceの生パース結果。未登録ならここで読んで登録する(読込と先読みは同じ読込オプション)。
    // 別スレッドが同じキーを読み込み中なら、その完了を待って同じ結果を受け取る。
    // 戻り値は共有所有なので、無効化で表から消えても呼び出し側の参照は切れない。
    std::shared_ptr<const rs::legacy_plugin::LoadedPlugin> EnsureParsed(
        const PluginSource& source, bool* cacheHit = nullptr,
        const rs::legacy_plugin::LoadOptions& options = {});

    // 未開始の先読みを取り消し、処理中の読込が終わるまで待つ。
    void CancelPrefetch();
    // 再走査は依存先の別フォルダも変えうるため、共有する読込結果を全て捨てる。
    // 読込中の結果は表に戻らない(読込中の呼び出し側にはそのまま渡る)。
    void Invalidate();
    // メッシュキーに混ぜる世代番号。毎フレーム読まれるためロックを取らない。
    uint64_t Generation() const { return generation_.load(std::memory_order_acquire); }

    // sourceListのうち、表に未登録のものだけをワーカースレッドへ投入する(非ブロッキング)。
    void RequestPrefetch(const std::vector<PluginSource>& sourceList);

    // 終わった先読みのスレッドを片付ける(メインスレッド専用)。結果はワーカーが直接表へ登録する。
    void HarvestCompletedPrefetch();

    // sourceの読込が完了して表に登録済みか(先読み・通常の読込のどちらでも)。
    bool Contains(const PluginSource& source) const;

private:
    using ParsedPtr = std::shared_ptr<const rs::legacy_plugin::LoadedPlugin>;
    // 1キー分の読込結果。最初に登録したスレッドだけが解析してpromiseを埋め、
    // ほかのスレッドはロックの外でfutureを待つ(同じプラグインを二重に解析しない)。
    struct Slot {
        std::promise<ParsedPtr> promise;
        std::shared_future<ParsedPtr> future = promise.get_future().share();
    };
    // keyの枠を引く。無ければ登録して ownerOut=true を返す。ロックはこの中だけで取る。
    std::shared_ptr<Slot> AcquireSlot(const std::string& key, bool& ownerOut);
    // 担当スレッドが解析して枠を埋める。例外は枠へ入れ、表に残っていれば外す。
    void FillSlot(const std::string& key, const std::shared_ptr<Slot>& slot, const PluginSource& source,
                  const rs::legacy_plugin::LoadOptions& options);

    // entries_の検索・登録だけを守る。解析とセッションへのコピーはロックの外で行う。
    mutable std::mutex mutex_;
    std::atomic<uint64_t> generation_{0};
    std::atomic<bool> cancelPrefetch_{false};
    // 先読み1回分のスレッド群。remainingが0になったものから片付ける。
    struct PrefetchBatch {
        std::shared_ptr<std::atomic<size_t>> remaining;
        rs::platform_thread::ParallelJobs jobs;
    };
    std::vector<PrefetchBatch> prefetchJobs_;

    // evictionは無い。保持するのはスイッチ焼き込み前のLoadedPluginだけ。キーはParseKey。
    std::unordered_map<std::string, std::shared_ptr<Slot>> entries_;
};

// アプリ内の読込結果と先読みの唯一の持ち主。終了時まで保持する。
ParsedPluginCache& GetParsedPluginCache();
std::string MakePluginMeshKey(const std::string& contentKey);

} // namespace rs::app
