// SoundEffectAttachment.h - SoundEffect AttachObject のロード時解決。
#pragma once

#include <string>
#include <vector>

#include "../../../core/Math.h"
#include "../../schema/TriggerInfo.h"

namespace rs::legacy_pi { struct PluginDefinition; }

namespace rs::app {

struct SoundEffectAttachment {
    std::string waveFile;
    std::wstring resolvedWavPath;
    int partDefIndex = -1;
    rs::core::Vec3 sourceCoord{0, 0, 0};
    int volume = 0;
    bool loop = true;
    // 発動文脈(If/ApplySwitch条件)。条件から外れたフレームは再生対象から除外する
    // (呼び出し側=PlacedSoundRequestCollectorがrs::legacy_pi::IsTriggerActiveで判定する)。
    rs::legacy_pi::TriggerInfo trig;
};

// AttachObject のラベルが指す部品の添字。見つからなければ -1。灯具の解決(HeadlightAttachment)も使う。
int FindPartIndexByLabel(const rs::legacy_pi::PluginDefinition& def, const std::string& label);

void BuildSoundEffectAttachments(const rs::legacy_pi::PluginDefinition& def,
                                 const std::string& folderUtf8,
                                 std::vector<SoundEffectAttachment>& out);

} // namespace rs::app
