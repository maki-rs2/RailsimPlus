#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace rs::rhi {

// フェンスは所有するキューの提出順で進め、登録した資源と記述子を一緒に保持する。
class RetirementQueue {
public:
    void SetRetireFenceValue(uint64_t fenceValue) { retireFenceValue_ = fenceValue; }
    void Retire(std::shared_ptr<void> resource, std::function<void()> release = {});
    void ReleaseCompleted(uint64_t completedFenceValue);

private:
    struct Entry {
        uint64_t fenceValue;
        std::shared_ptr<void> resource;
        std::function<void()> release;
    };
    uint64_t retireFenceValue_ = 1;
    std::vector<Entry> entries_;
};

} // namespace rs::rhi
