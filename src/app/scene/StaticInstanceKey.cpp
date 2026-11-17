//
// StaticInstanceKey.cpp
//
#include "StaticInstanceKey.h"
#include "../../plugins/load/PluginPaths.h"
#include "../../platform/text/AsciiCase.h"
#include "../../world/scene/SceneEntity.h"

namespace rs::app {

bool PluginSource::operator==(const PluginSource& other) const {
    return MakePluginSourceKey(*this) == MakePluginSourceKey(other);
}

std::string MakePluginSourceKey(const std::string& folderUtf8, const std::string& definitionFile) {
    const std::string folder = rs::legacy_plugin::NormalizePluginFolderKey(folderUtf8);
    return definitionFile.empty() ? folder : folder + '?' + rs::platform_text::ToAsciiLower(definitionFile);
}

std::string MakeStaticInstanceKey(const std::string& folderUtf8,
                                  const std::vector<int32_t>& switchOptions, bool signalModel,
                                  const std::string& definitionFile) {
    // 正規化しないと「スイッチ指定なし」と「全て0を明示」が別キーになり、同一の
    // ジオメトリ/テクスチャを二重にアップロードしてしまう(レイアウト側は指定を
    // 省略でき、編集UIは全スイッチ分の配列を渡すため両方の形が混在する)。
    size_t count = switchOptions.size();
    while (count > 0 && switchOptions[count - 1] == 0)
        --count;

    const std::string source = MakePluginSourceKey(folderUtf8, definitionFile);
    std::string key = signalModel ? "signal:" + source : source;
    key += '|';
    for (size_t i = 0; i < count; ++i) {
        key += std::to_string(switchOptions[i]);
        key += ',';
    }
    return key;
}

std::string MakeSlotContentKey(const rs::world::SceneEntity& entity) {
    return MakeStaticInstanceKey(entity.folderUtf8, entity.switchOptions, /*signalModel=*/false,
                                 entity.definitionFile);
}

std::string MakeStaticVariantKey(const std::string& baseKey, size_t variantIndex,
                                 size_t variantCount) {
    if (variantCount <= 1) return baseKey;
    return baseKey + "|cd" + std::to_string(variantIndex);
}

} // namespace rs::app
