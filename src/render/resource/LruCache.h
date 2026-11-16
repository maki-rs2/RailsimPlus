#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <list>
#include <mutex>
#include <unordered_map>

namespace rs::render {

// パス等のキー+mtimeキーのLRUキャッシュ共通実装。ModelManager/ImageCache(TextureLoader.cpp)が使う、
// 「バイト予算超過でLRU末尾から追い出す」共通実装。
// 呼び出し側は必ずコピーで値を受け取る前提
// (Insert後にキャッシュ内部でevictされても呼び出し側の値には影響しない)。
// スレッドセーフ(mutex内蔵)。
template <typename Key, typename Value>
class LruCache {
public:
    using SizeFn = std::function<size_t(const Value&)>;

    // budgetBytes: 保持する合計バイト数の上限(sizeFnの合計がこれを超えたらLRU末尾から追い出す)。
    // sizeFn: 1エントリのバイト数概算を返す関数(呼び出し側のApproxBytes相当)。
    LruCache(size_t budgetBytes, SizeFn sizeFn)
        : budgetBytes_(budgetBytes), sizeFn_(std::move(sizeFn)) {}

    // keyを引き、mtimeが完全一致すればoutへコピーしてtrueを返す(ヒットはLRU先頭へ移動)。
    // 不一致/未登録ならfalseを返す(呼び出し側でパース等のミス処理を行い、Insertする)。
    bool TryGet(const Key& key, std::filesystem::file_time_type mtime, Value& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        typename std::unordered_map<Key, typename std::list<Entry>::iterator>::iterator it =
            map_.find(key);
        if (it == map_.end() || it->second->mtime != mtime) return false;
        lru_.splice(lru_.begin(), lru_, it->second); // ヒットを MRU へ
        out = it->second->value; // コピー返し
        return true;
    }

    // 挿入(既存キーは置き換え)。挿入後、合計バイト数がbudgetBytes_を超えていれば
    // LRU末尾(最も長く使われていない)から`lru_.size() > 1`を保ったまま追い出す。
    void Insert(const Key& key, std::filesystem::file_time_type mtime, const Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        typename std::unordered_map<Key, typename std::list<Entry>::iterator>::iterator it =
            map_.find(key);
        if (it != map_.end()) { // 別スレッドが先に入れた / mtime 更新で再挿入
            bytes_ -= it->second->bytes;
            lru_.erase(it->second);
            map_.erase(it);
        }
        Entry e;
        e.key = key;
        e.mtime = mtime;
        e.value = value;
        e.bytes = sizeFn_(value);
        lru_.push_front(std::move(e));
        map_[key] = lru_.begin();
        bytes_ += lru_.begin()->bytes;

        // 少なくとも1件は残す(予算より大きい単一エントリで延々と追い出さない)。
        while (bytes_ > budgetBytes_ && lru_.size() > 1) {
            Entry& last = lru_.back();
            bytes_ -= last.bytes;
            map_.erase(last.key);
            lru_.pop_back();
        }
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.clear();
        lru_.clear();
        bytes_ = 0;
    }

    // 現在の滞留バイト数(sizeFn_の合計)。予算に対してどれだけ埋まっているかの計測用。
    size_t Bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return bytes_;
    }

private:
    struct Entry {
        Key                              key;
        std::filesystem::file_time_type  mtime;
        Value                            value;
        size_t                           bytes = 0;
    };

    mutable std::mutex                                          mutex_;
    size_t                                                      budgetBytes_;
    SizeFn                                                      sizeFn_;
    size_t                                                      bytes_ = 0;
    std::list<Entry>                                            lru_;
    std::unordered_map<Key, typename std::list<Entry>::iterator> map_;
};

} // namespace rs::render
