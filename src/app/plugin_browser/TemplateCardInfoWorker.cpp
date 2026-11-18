#include "TemplateCardInfoWorker.h"

#include <utility>

namespace rs::app::plugin_browser {

namespace {
constexpr int kCardInfoPriority = 0; // 優先度は使い分けない(受付の順に処理する)
constexpr std::size_t kDrainAll = static_cast<std::size_t>(-1);
} // namespace

TemplateCardInfoWorker::TemplateCardInfoWorker(TemplateParseFunc parse)
    : parse_(parse ? std::move(parse) : TemplateParseFunc(ParseTrainGroupTemplateFile)),
      worker_([this](const TemplateCardInfoRequest& request, std::stop_token) { return Process(request); }) {}

std::uint64_t TemplateCardInfoWorker::Begin() {
    worker_.ClearPending();
    return ++round_;
}

void TemplateCardInfoWorker::Post(std::uint64_t round, std::size_t row, const TemplateImageSource& source,
                                  std::uint64_t generation) {
    worker_.Post(TemplateCardInfoRequest{round, row, generation, source}, kCardInfoPriority);
}

std::vector<TemplateCardInfoResult> TemplateCardInfoWorker::Drain(std::uint64_t generation) {
    std::vector<TemplateCardInfoResult> results = worker_.Drain(kDrainAll);
    std::erase_if(results, [this, generation](const TemplateCardInfoResult& result) {
        return result.round != round_ || result.generation != generation;
    });
    return results;
}

TemplateCardInfoResult TemplateCardInfoWorker::Process(const TemplateCardInfoRequest& request) const {
    TemplateCardInfoResult result;
    result.round = request.round;
    result.row = request.row;
    result.generation = request.generation;
    const TemplateCardInfoCacheKey key = MakeTemplateCardInfoCacheKey(request.source, request.generation);
    if (const std::optional<TemplateCardInfo> hit = cache_.Find(key)) {
        result.info = *hit;
        return result;
    }
    result.info = ComputeTemplateCardInfo(request.source, parse_);
    cache_.Store(key, result.info);
    return result;
}

} // namespace rs::app::plugin_browser
