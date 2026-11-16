//
// TextureStep.cpp - ModelPart::textureSteps の追加と参照の補助
//
#include "PluginSchema.h"

#include <utility>

namespace rs::legacy_pi {

const TriggerInfo& TextureStep::Trig() const {
    switch (kind) {
    case Kind::ChangeTexture: return changeTexture.trig;
    case Kind::SetAnimation:  return setAnimation.trig;
    case Kind::Transform:     return transform.trig;
    }
    return changeTexture.trig;
}

void ModelPart::AddChangeTexture(ChangeTextureRef value) {
    TextureStep step;
    step.kind = TextureStep::Kind::ChangeTexture;
    step.changeTexture = std::move(value);
    textureSteps.push_back(std::move(step));
}

void ModelPart::AddSetAnimation(SetAnimationCustomizer value) {
    TextureStep step;
    step.kind = TextureStep::Kind::SetAnimation;
    step.setAnimation = std::move(value);
    textureSteps.push_back(std::move(step));
}

void ModelPart::AddTextureTransform(TextureTransformCustomizer value) {
    TextureStep step;
    step.kind = TextureStep::Kind::Transform;
    step.transform = std::move(value);
    textureSteps.push_back(std::move(step));
}

size_t ModelPart::ChangeTextureCount() const {
    size_t count = 0;
    for (const TextureStep& step : textureSteps) {
        if (step.kind == TextureStep::Kind::ChangeTexture) ++count;
    }
    return count;
}

} // namespace rs::legacy_pi
