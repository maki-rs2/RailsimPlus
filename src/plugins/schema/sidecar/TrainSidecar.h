//
// TrainSidecar.h - 列車向けプラグイン拡張JSONの読込結果
//
#pragma once

#include <string>
#include <vector>

#include "../parser/Diagnostics.h"
#include "PluginSidecarCommon.h"

namespace rs::legacy_pi::sidecar {

constexpr const char* kTrainSidecarFileName = "train.json";

struct TrainSidecar {
    bool loaded = false;
    bool hasSounds = false;
    std::string soundsRawJson;
    SwitchMappingTable table;
    std::vector<Diagnostic> diagnostics;
};

// pluginFolder直下のtrain.jsonを読む。ファイルが無い場合は診断を残さない。
TrainSidecar LoadTrainSidecar(const std::wstring& pluginFolder);

} // namespace rs::legacy_pi::sidecar
