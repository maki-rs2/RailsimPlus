//
// RoutineListEdit.h - ルーチン表の vector 操作(番線と分岐で、要素の型だけが違う同じ形の編集)
//
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace rs::app::routine_list_edit {

// 要素を取り出し、挿入後に toIndex の位置へ来るようにする。範囲外・同じ位置なら false。
template <typename T>
bool MoveWithin(std::vector<T>& values, std::size_t fromIndex, std::size_t toIndex) {
    if (fromIndex >= values.size() || toIndex >= values.size() || fromIndex == toIndex) return false;
    T moved = std::move(values[fromIndex]);
    values.erase(values.begin() + static_cast<std::ptrdiff_t>(fromIndex));
    values.insert(values.begin() + static_cast<std::ptrdiff_t>(toIndex), std::move(moved));
    return true;
}

// 隣(delta 個ぶん離れた位置)の要素と入れ替える。範囲外なら false。
template <typename T>
bool MoveBy(std::vector<T>& values, std::size_t index, int delta) {
    const std::ptrdiff_t target = static_cast<std::ptrdiff_t>(index) + delta;
    if (index >= values.size() || target < 0 || target >= static_cast<std::ptrdiff_t>(values.size())) return false;
    std::swap(values[index], values[static_cast<std::size_t>(target)]);
    return true;
}

template <typename T>
bool EraseAt(std::vector<T>& values, std::size_t index) {
    if (index >= values.size()) return false;
    values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

// 層の持ち主を表すキー(タグ番号・編成のGuid)で、層の項目を探す。無ければ nullptr。
template <typename Entry, typename Key>
Entry* FindByKey(std::vector<Entry>& entries, Key Entry::*member, const Key& key) {
    for (Entry& entry : entries) {
        if (entry.*member == key) return &entry;
    }
    return nullptr;
}

template <typename Entry, typename Key>
const Entry* FindByKey(const std::vector<Entry>& entries, Key Entry::*member, const Key& key) {
    for (const Entry& entry : entries) {
        if (entry.*member == key) return &entry;
    }
    return nullptr;
}

// 同じキーの層がなければ末尾へ足して true。すでにあれば false。
template <typename Entry, typename Key>
bool AddIfAbsent(std::vector<Entry>& entries, Key Entry::*member, const Key& key, Entry entry) {
    if (FindByKey(entries, member, key) != nullptr) return false;
    entries.push_back(std::move(entry));
    return true;
}

} // namespace rs::app::routine_list_edit
