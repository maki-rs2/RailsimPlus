#include "InitSteps.h"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace rs::app {

bool InitStepsResult::IsDisabled(const std::string& name) const {
    return std::find(disabledOptional.begin(), disabledOptional.end(), name) != disabledOptional.end();
}

InitStepsResult RunInitSteps(std::span<const InitStep> steps) {
    InitStepsResult result;
    for (const InitStep& step : steps) {
        if (step.run && step.run()) continue;
        if (step.importance == InitImportance::Required) {
            spdlog::error("[init] 必須の「{}」を初期化できませんでした", step.name);
            result.ok = false;
            result.failedRequired = step.name;
            return result;
        }
        spdlog::warn("[init] 任意の「{}」を初期化できなかったため、無効にして続けます", step.name);
        result.disabledOptional.emplace_back(step.name);
    }
    return result;
}

} // namespace rs::app
