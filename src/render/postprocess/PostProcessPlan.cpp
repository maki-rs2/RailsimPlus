#include "PostProcessPlan.h"

namespace rs::render {

PostProcessPlan PlanStages(const PostProcessSettings& settings, const PostProcessTuning& tuning,
                           const TonemapParams& tonemapParams) {
    PostProcessPlan plan;
    plan.bloom = settings.bloom && tuning.bloomStrength > 0.0f && tonemapParams.mode != TonemapMode::Off;
    plan.fxaa = settings.aa == AntiAliasing::Fxaa;
    return plan;
}

} // namespace rs::render
