// SoundEffectAttachment.cpp - SoundEffect AttachObject のロード時解決。
#include "SoundEffectAttachment.h"

#include <utility>

#include <spdlog/spdlog.h>

#include "../../schema/PluginSchema.h"
#include "../PluginPaths.h"
#include "../../../platform/text/Encoding.h"

namespace rs::app {

int FindPartIndexByLabel(const rs::legacy_pi::PluginDefinition& def, const std::string& label) {
    for (size_t i = 0; i < def.parts.size(); ++i) {
        if (def.parts[i].label == label) return static_cast<int>(i);
    }
    return -1;
}

void BuildSoundEffectAttachments(const rs::legacy_pi::PluginDefinition& def,
                                 const std::string& folderUtf8,
                                 std::vector<SoundEffectAttachment>& out) {
    for (const rs::legacy_pi::SoundEffectRef& soundEffect : def.soundEffects) {
        const int partDefIndex = FindPartIndexByLabel(def, soundEffect.attachObject);
        if (partDefIndex < 0) {
            spdlog::warn("[sound_effect_attachment] skipped AttachObject '{}': part was not found",
                         soundEffect.attachObject);
            continue;
        }

        const rs::legacy_pi::ModelPart& part = def.parts[static_cast<size_t>(partDefIndex)];
        if (part.hasUnresolvedJointRef) {
            spdlog::warn("[sound_effect_attachment] AttachObject '{}' has an unresolved Joint3D reference",
                         soundEffect.attachObject);
        }

        SoundEffectAttachment attachment;
        attachment.waveFile = soundEffect.waveFile;
        if (!folderUtf8.empty()) {
            attachment.resolvedWavPath = rs::legacy_plugin::ResolvePath(
                rs::platform_text::Utf8ToWide(folderUtf8), soundEffect.waveFile);
        }
        attachment.partDefIndex = partDefIndex;
        attachment.sourceCoord = soundEffect.sourceCoord;
        attachment.volume = soundEffect.volume;
        attachment.loop = soundEffect.loop;
        attachment.trig = soundEffect.trig;
        out.push_back(std::move(attachment));
    }
}

} // namespace rs::app
