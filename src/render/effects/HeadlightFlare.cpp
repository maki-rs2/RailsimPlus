#include "HeadlightFlare.h"

#include <algorithm>

namespace rs::render {

namespace {

// またたきの乱数のシードの先頭。太陽は 1 を使うので、灯具は 2 から灯具の番号を足す。
constexpr std::uint32_t kHeadlightTwinkleSeedBase = 2;

// 要素が出る灯具1件。
struct Candidate {
    float distanceSq = 0.0f;
    rs::core::Vec3 position;
    std::vector<FlareInstance> instances;
};

}  // namespace

std::size_t AppendHeadlightFlares(const std::vector<HeadlightLight>& lights, const rs::core::Vec3& cameraPos,
                                  const rs::core::Vec3& cameraForward, std::uint32_t frameIndex,
                                  LensFlareFrame& frame) {
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const HeadlightLight& light = lights[i];
        if (!light.style || light.style->elements.empty()) continue;
        const rs::core::Vec3 toward = light.position - cameraPos;
        float alpha = ComputeFlareAlpha(light.style->startCos, light.direction, toward, 1.0f);
        if (alpha <= 0.0f) continue;
        const std::uint32_t seed = kHeadlightTwinkleSeedBase + static_cast<std::uint32_t>(i);
        alpha = ApplyTwinkle(alpha, light.style->twinkle, HashRandom01(frameIndex, seed));
        Candidate candidate;
        candidate.instances = LayoutFlareElements(*light.style, alpha, light.position, light.direction, cameraPos,
                                                  cameraForward, light.maxDistance);
        if (candidate.instances.empty()) continue;
        candidate.distanceSq = rs::core::Vec3::Dot(toward, toward);
        candidate.position = light.position;
        candidates.push_back(std::move(candidate));
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) { return a.distanceSq < b.distanceSq; });

    std::size_t dropped = 0;
    for (const Candidate& candidate : candidates) {
        const bool lightsFull = frame.lights.size() >= kMaxFlareLights;
        const bool instancesFull = frame.draws.size() + candidate.instances.size() > kMaxFlareInstances;
        if (lightsFull || instancesFull) {
            ++dropped;
            continue;
        }
        const std::uint32_t lightIndex = static_cast<std::uint32_t>(frame.lights.size());
        frame.lights.push_back({candidate.position, false});
        for (const FlareInstance& instance : candidate.instances) frame.draws.push_back({instance, lightIndex});
    }
    return dropped;
}

}  // namespace rs::render
