#pragma once

#include <memory>
#include <unordered_map>

namespace rs::render {

// basePath 等をキーにした weak_ptr キャッシュの共通実装(TextureManager /
// GeometryManager が使う)。
//
// スレッドセーフではない(mutex を持たない)。呼び出し側が既にロックを
// 取得している前提で Find/Insert を呼ぶこと(呼び出し側は cache_ 以外の
// 状態(失敗判定・プレースホルダー等)も同じロック区間で扱うことがあるため、
// このクラス自身がロックを持つと二重ロックやロック粒度の変更を招く)。
//
// Key がタプル等 std::hash に既定の特殊化を持たない型の場合、Hash を
// 明示指定すること(既定は std::hash<Key>)。
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class WeakPtrCache {
public:
    // 生存中の値があれば返す。全参照が切れた期限切れエントリ(シーン破棄後の
    // 再読込等)は検索時に削除する。
    std::shared_ptr<Value> Find(const Key& key) {
        typename std::unordered_map<Key, std::weak_ptr<Value>, Hash>::iterator it =
            cache_.find(key);
        if (it == cache_.end()) return nullptr;
        std::shared_ptr<Value> resident = it->second.lock();
        if (resident) return resident;
        cache_.erase(it);
        return nullptr;
    }

    void Insert(const Key& key, const std::shared_ptr<Value>& value) {
        cache_[key] = value;
    }

    // 期限切れも含めて全エントリを捨てる(値の寿命には触れない)。
    void Clear() { cache_.clear(); }

private:
    std::unordered_map<Key, std::weak_ptr<Value>, Hash> cache_;
};

} // namespace rs::render
