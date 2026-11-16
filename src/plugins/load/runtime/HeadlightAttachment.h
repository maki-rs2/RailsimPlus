// HeadlightAttachment.h - Headlight(灯具)の AttachObject のロード時解決。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../../../core/Math.h"
#include "../../schema/PluginSchema.h"

namespace rs::app {

struct HeadlightAttachment {
    int partDefIndex = -1;
    rs::core::Vec3 sourceCoord{0, 0, 0};
    rs::core::Vec3 direction{0, 0, 1};
    float maxDistance = 0.0f;
    // フレアの定義。Texture 要素のファイル名は、定義のフォルダから解決した絶対パス(UTF-8)に置き換えてある
    // (画像のスロットの台帳がこのパスをキーにするため)。
    rs::legacy_pi::LensFlareDef flare;
    // 要素と色と角度が同じ定義は同じ文字列になる。フレアのスタイルの共有キー。
    std::string styleKey;
    // 発動文脈(If/Else/ApplySwitch条件)。偽のフレームは出さない。
    rs::legacy_pi::TriggerInfo trig;
};

// フレアの定義を持たない灯具(LensFlare を省き、直前の灯具も無いもの)は、描くものが無いので作らない。
void BuildHeadlightAttachments(const rs::legacy_pi::PluginDefinition& def, const std::string& folderUtf8,
                               std::vector<HeadlightAttachment>& out);

// フレアの定義の内容から、共有キーを作る(同じ内容なら同じ文字列)。
std::string MakeFlareStyleKey(const rs::legacy_pi::LensFlareDef& flare);

} // namespace rs::app
