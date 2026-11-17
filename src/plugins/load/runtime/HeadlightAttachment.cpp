// HeadlightAttachment.cpp - Headlight(灯具)の AttachObject のロード時解決。
#include "HeadlightAttachment.h"

#include <utility>

#include <spdlog/spdlog.h>

#include "../../../platform/text/Encoding.h"
#include "../PluginPaths.h"
#include "SoundEffectAttachment.h"
#include "../../schema/PluginSchema.h"

namespace rs::app {

std::string MakeFlareStyleKey(const rs::legacy_pi::LensFlareDef& flare) {
    std::string key = std::to_string(flare.startAngle) + "|" + std::to_string(flare.twinkle) + "|" +
                      std::to_string(flare.inclination);
    for (const rs::legacy_pi::LensFlareElement& element : flare.elements) {
        key += "|" + std::to_string(static_cast<int>(element.kind)) + "," + std::to_string(element.distance) + "," +
               std::to_string(element.radius);
        for (const rs::legacy_pi::PiColor& color : {element.innerColor, element.outerColor}) {
            key += "," + std::to_string(color.r) + "," + std::to_string(color.g) + "," + std::to_string(color.b) +
                   "," + std::to_string(color.a);
        }
        if (element.kind == rs::legacy_pi::LensFlareElement::Kind::Texture) key += "," + element.texFileName;
    }
    return key;
}

void BuildHeadlightAttachments(const rs::legacy_pi::PluginDefinition& def, const std::string& folderUtf8,
                               std::vector<HeadlightAttachment>& out) {
    for (const rs::legacy_pi::HeadlightDef& headlight : def.headlights) {
        if (!headlight.lensFlare.has_value()) continue;
        const int partDefIndex = FindPartIndexByLabel(def, headlight.attachObject);
        if (partDefIndex < 0) {
            spdlog::warn("[headlight_attachment] skipped AttachObject '{}': part was not found",
                         headlight.attachObject);
            continue;
        }

        HeadlightAttachment attachment;
        attachment.partDefIndex = partDefIndex;
        attachment.sourceCoord = headlight.sourceCoord;
        attachment.direction = headlight.direction;
        attachment.maxDistance = static_cast<float>(headlight.maxDistance);
        attachment.flare = *headlight.lensFlare;
        if (!folderUtf8.empty()) {
            const std::wstring folderWide = rs::platform_text::Utf8ToWide(folderUtf8);
            for (rs::legacy_pi::LensFlareElement& element : attachment.flare.elements) {
                if (element.kind != rs::legacy_pi::LensFlareElement::Kind::Texture) continue;
                element.texFileName = rs::platform_text::WideToUtf8(
                    rs::legacy_plugin::ResolvePath(folderWide, element.texFileName));
            }
        }
        attachment.styleKey = MakeFlareStyleKey(attachment.flare);
        attachment.trig = headlight.trig;
        out.push_back(std::move(attachment));
    }
}

} // namespace rs::app
