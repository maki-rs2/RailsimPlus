#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

#include "LruCache.h"
#include "RenderTuning.h"
#include "XLoader.h"
#include "../../platform/text/Encoding.h"

namespace rs::render {

// パス(+更新時刻)キーのパース済みメッシュ LRU キャッシュ。同一 .x を複数パーツ
// /ChangeModel 候補/プラグイン跨ぎで参照した際の assimp 再パースを除去する
// (ChangeModel は base と候補で同一ファイルを二重ロードする実装のため特に効果が大きい)。
// 返却はコピー(呼び出し側が mesh を move/変形するため)。非同期ロードからも
// 使えるようミューテックスで保護し、バイト予算超過分は LRU で追い出す。
// LRU本体は共通実装 LruCache<Key,Value> に委譲する。
class ModelManager {
public:
    ModelManager()
        : cache_(static_cast<size_t>(RenderTuning::Embedded().modelCacheBudgetBytes),
                 &ModelManager::ApproxBytes) {}

    rs::legacy_x::LoadResult Get(const std::string& utf8Path,
                                  const std::function<rs::legacy_x::LoadResult()>& parse) {
        std::error_code ec;
        std::filesystem::path fp(rs::platform_text::Utf8ToWide(utf8Path));
        std::filesystem::file_time_type mtime = std::filesystem::last_write_time(fp, ec);

        const std::chrono::steady_clock::time_point tHit0 = std::chrono::steady_clock::now();
        rs::legacy_x::LoadResult copy;
        if (cache_.TryGet(utf8Path, mtime, copy)) {
            std::lock_guard<std::mutex> lock(statsMutex_);
            ++stats_.hits;
            stats_.hitMs += std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - tHit0)
                                .count();
            return copy;
        }

        const std::chrono::steady_clock::time_point tMiss0 = std::chrono::steady_clock::now();
        rs::legacy_x::LoadResult res = parse();
        if (!res.ok) return res; // 失敗はキャッシュしない
        cache_.Insert(utf8Path, mtime, res);
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            ++stats_.misses;
            stats_.missMs += std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - tMiss0)
                                 .count();
        }
        return res;
    }

    // 計測専用。ヒット(=コピーのみ)とミス(=パース+挿入)の内訳を切り分ける。
    struct Stats {
        int    hits = 0;
        int    misses = 0;
        double hitMs = 0.0;
        double missMs = 0.0;
    };
    Stats GetStats() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        return stats_;
    }

    void Clear() {
        cache_.Clear();
    }

    // 計測専用。現在の滞留バイト数と設定済み予算。
    size_t Bytes() const { return cache_.Bytes(); }
    static size_t BudgetBytes() {
        return static_cast<size_t>(RenderTuning::Embedded().modelCacheBudgetBytes);
    }

private:
    static size_t ApproxBytes(const rs::legacy_x::LoadResult& r);

    LruCache<std::string, rs::legacy_x::LoadResult> cache_;

    mutable std::mutex statsMutex_;
    Stats               stats_;
};

ModelManager& GetModelManager();

} // namespace rs::render
