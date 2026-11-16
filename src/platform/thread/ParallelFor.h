//
// ParallelFor.h - 並列処理の共通部品(添字を取り合う使い捨てワーカー群)
//
#pragma once

#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

namespace rs::platform_thread {

// jobCount件のジョブを並列処理する際のワーカースレッド数を決める(ハードウェア
// 論理コア数の半分、下限2・上限jobCount)。
unsigned int DecideWorkerCount(size_t jobCount);

// job(0)〜job(jobCount-1)を並列に1回ずつ実行し、全件終わるまで待つ。
// jobCountが0なら何もせず、1なら呼び出しスレッドで直接実行する。
void ParallelFor(size_t jobCount, const std::function<void(size_t)>& job);

// StartParallelForが起動したスレッド群。Join()かデストラクタで必ずjoinする
// (detachしない。ジョブが参照するデータより先に破棄しないこと)。
class ParallelJobs {
public:
    ParallelJobs() = default;
    explicit ParallelJobs(std::vector<std::thread> threads) : threads_(std::move(threads)) {}
    ParallelJobs(ParallelJobs&&) noexcept = default;
    ParallelJobs& operator=(ParallelJobs&& other) noexcept;
    ParallelJobs(const ParallelJobs&) = delete;
    ParallelJobs& operator=(const ParallelJobs&) = delete;
    ~ParallelJobs() { Join(); }

    // 全スレッドの終了を待つ。2回目以降は何もしない。
    void Join();

private:
    std::vector<std::thread> threads_;
};

// ParallelForと同じ分担で、待たずに戻る。jobはスレッド間で共有するためコピーして保持する。
// jobCountが0なら空のParallelJobsを返す。
ParallelJobs StartParallelFor(size_t jobCount, std::function<void(size_t)> job);

} // namespace rs::platform_thread
