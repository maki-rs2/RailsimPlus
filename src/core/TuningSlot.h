// TuningSlot.h - 調整値の「現在の版」を、読む側へ参照で返しながら差し替えられる入れ物。
#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <utility>

namespace rs::core {

// 版を積んでポインタを差し替える。古い版は解放しないので、Publish より前に取った参照や、
// 別スレッド(撮影のワーカー等)が持っている参照が指す先は消えない。
// Current は任意のスレッドから呼べる。Publish は1つのスレッドからだけ呼ぶ(版の一覧に鍵を掛けないため)。
// 版は Publish のたびに増える。想定は開発中の読み直しで、回数は多くない。
template <typename T>
class TuningSlot {
public:
    explicit TuningSlot(T initial) {
        versions_.push_back(std::make_unique<const T>(std::move(initial)));
        current_.store(versions_.back().get(), std::memory_order_release);
    }

    TuningSlot(const TuningSlot&) = delete;
    TuningSlot& operator=(const TuningSlot&) = delete;

    const T& Current() const { return *current_.load(std::memory_order_acquire); }

    // 新しい版を公開する。以降の Current はこの版を返す(すでに取った参照は古い版のまま有効)。
    void Publish(T value) {
        versions_.push_back(std::make_unique<const T>(std::move(value)));
        current_.store(versions_.back().get(), std::memory_order_release);
    }

private:
    // deque は push_back しても既存の要素が動かない。各版は unique_ptr の先にあるのでどのみち動かない。
    std::deque<std::unique_ptr<const T>> versions_;
    std::atomic<const T*> current_{nullptr};
};

} // namespace rs::core
