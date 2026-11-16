//
// WorkerThread.h - 常駐ワーカー1本と、優先度つきの受付・出口を持つ部品
//
#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace rs::platform_thread {

// Postされた受付をワーカースレッド1本が優先度順に処理し、結果を出口へ溜める。
// 出口はDrainで呼び出し側(メインスレッド等)が取り出す。
// processは受付1件につき1回、ワーカースレッドで呼ばれる。長い処理は渡された
// stop_tokenを見て途中で切り上げてよい(デストラクタが止める要求を出す)。
template <class Request, class Result>
class WorkerThread {
public:
    using ProcessFunc = std::function<Result(const Request&, std::stop_token)>;

    // onThreadStart / onThreadEnd: ワーカースレッドの開始直後・終了直前に1回ずつ呼ぶ
    // (COM初期化など、スレッド単位の準備と後始末用)。空なら呼ばない。
    explicit WorkerThread(ProcessFunc process, std::function<void()> onThreadStart = {},
                          std::function<void()> onThreadEnd = {})
        : process_(std::move(process)),
          onThreadStart_(std::move(onThreadStart)),
          onThreadEnd_(std::move(onThreadEnd)),
          thread_([this](std::stop_token stopToken) { Run(stopToken); }) {}

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;
    WorkerThread(WorkerThread&&) = delete;
    WorkerThread& operator=(WorkerThread&&) = delete;

    // 止める要求を出し、処理中の1件が終わるまで待ってjoinする。
    ~WorkerThread() {
        thread_.request_stop();
        thread_.join();
    }

    // 小さいpriorityほど先に処理する。同じpriorityは先着順。
    void Post(Request request, int priority) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.emplace(Key{priority, nextSequence_++}, std::move(request));
        }
        cv_.notify_one();
    }

    // まだ処理を始めていない受付を捨てる(処理中の1件は止めない)。
    void ClearPending() {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.clear();
    }

    // まだ処理を始めていない受付のうち、pred(const Request&)が真のものだけを捨てる(処理中の1件は止めない)。
    template <class Pred>
    void ClearPendingIf(Pred pred) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::erase_if(pending_, [&pred](const typename std::map<Key, Request>::value_type& item) {
            return pred(item.second);
        });
    }

    // 出口から最大maxCount件を、出来上がった順に取り出す。
    std::vector<Result> Drain(size_t maxCount) {
        std::vector<Result> out;
        std::lock_guard<std::mutex> lock(mutex_);
        while (out.size() < maxCount && !results_.empty()) {
            out.push_back(std::move(results_.front()));
            results_.pop_front();
        }
        return out;
    }

    // 受付が空で、処理中でもないときtrue。出口に未回収の結果が残っていてもtrueになる。
    bool Idle() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_.empty() && !busy_;
    }

private:
    using Key = std::pair<int, std::uint64_t>; // (priority, 連番)

    void Run(std::stop_token stopToken) {
        if (onThreadStart_) onThreadStart_();
        for (;;) {
            Request request;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!cv_.wait(lock, stopToken, [this] { return !pending_.empty(); })) break;
                typename std::map<Key, Request>::iterator it = pending_.begin();
                request = std::move(it->second);
                pending_.erase(it);
                busy_ = true;
            }
            Result result = process_(request, stopToken);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                results_.push_back(std::move(result));
                busy_ = false;
            }
        }
        if (onThreadEnd_) onThreadEnd_();
    }

    ProcessFunc process_;
    std::function<void()> onThreadStart_;
    std::function<void()> onThreadEnd_;

    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
    std::map<Key, Request> pending_;
    std::deque<Result> results_;
    std::uint64_t nextSequence_ = 0;
    bool busy_ = false;

    // 他のメンバが全て初期化されてから起動するため、最後に置く。
    std::jthread thread_;
};

} // namespace rs::platform_thread
