//
// ParallelFor.cpp
//
#include "ParallelFor.h"

#include <atomic>
#include <memory>
#include <utility>

namespace rs::platform_thread {

unsigned int DecideWorkerCount(size_t jobCount) {
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    unsigned int workerCount = hw / 2;
    if (workerCount < 2) workerCount = 2;
    if (workerCount > jobCount) workerCount = static_cast<unsigned int>(jobCount);
    return workerCount;
}

ParallelJobs& ParallelJobs::operator=(ParallelJobs&& other) noexcept {
    if (this != &other) {
        Join();
        threads_ = std::move(other.threads_);
        other.threads_.clear();
    }
    return *this;
}

void ParallelJobs::Join() {
    for (std::thread& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

void ParallelFor(size_t jobCount, const std::function<void(size_t)>& job) {
    if (jobCount == 0) return;
    if (jobCount == 1) {
        job(0);
        return;
    }
    StartParallelFor(jobCount, job).Join();
}

ParallelJobs StartParallelFor(size_t jobCount, std::function<void(size_t)> job) {
    if (jobCount == 0) return ParallelJobs();

    // 呼び出し元が先に戻ってもワーカーが参照できるよう、共有状態はshared_ptrで持つ。
    std::shared_ptr<const std::function<void(size_t)>> sharedJob =
        std::make_shared<const std::function<void(size_t)>>(std::move(job));
    std::shared_ptr<std::atomic<size_t>> nextIndex = std::make_shared<std::atomic<size_t>>(0);

    const unsigned int workerCount = DecideWorkerCount(jobCount);
    std::vector<std::thread> threads;
    threads.reserve(workerCount);
    for (unsigned int i = 0; i < workerCount; ++i) {
        threads.emplace_back([sharedJob, nextIndex, jobCount]() {
            for (;;) {
                const size_t idx = nextIndex->fetch_add(1, std::memory_order_relaxed);
                if (idx >= jobCount) break;
                (*sharedJob)(idx);
            }
        });
    }
    return ParallelJobs(std::move(threads));
}

} // namespace rs::platform_thread
