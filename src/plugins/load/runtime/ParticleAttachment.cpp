#include "ParticleAttachment.h"

#include <utility>

#include <spdlog/spdlog.h>

#include "../../../platform/text/Encoding.h"
#include "../PluginPaths.h"
#include "SoundEffectAttachment.h"

namespace rs::app {

void BuildParticleAttachments(const rs::legacy_pi::PluginDefinition& def, const std::string& folderUtf8,
                               std::vector<ParticleAttachment>& out) {
    for (const rs::legacy_pi::ParticleDef& particle : def.particles) {
        const int partDefIndex = FindPartIndexByLabel(def, particle.attachObject);
        if (partDefIndex < 0) {
            spdlog::warn("[particle_attachment] skipped AttachObject '{}': part was not found", particle.attachObject);
            continue;
        }
        ParticleAttachment attachment;
        attachment.partDefIndex = partDefIndex;
        attachment.sourceCoord = particle.sourceCoord;
        attachment.params = rs::sim::ParticleEmitterParams::FromDefinition(particle);
        attachment.texturePath = folderUtf8.empty() ? particle.textureFileName : rs::platform_text::WideToUtf8(
            rs::legacy_plugin::ResolvePath(rs::platform_text::Utf8ToWide(folderUtf8), particle.textureFileName));
        attachment.trig = particle.trig;
        out.push_back(std::move(attachment));
    }
}

} // namespace rs::app
