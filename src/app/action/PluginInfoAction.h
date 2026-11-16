//
// PluginInfoAction.h - 読込済みプラグイン情報のアプリケーション操作
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "SwitchList.h"

namespace rs::app {

struct PluginSession;

struct PluginInfoViewModel {
    bool loaded = false;
    std::string pluginName;
    std::string typeName;
    std::string author;
    std::size_t partCount = 0;
    int missingFiles = 0;
    std::vector<SwitchRowViewModel> switches;
};

class PluginInfoAction : public SwitchValueWriter {
public:
    explicit PluginInfoAction(PluginSession& session);

    // indexはDefineSwitchの宣言順。範囲外・未登録のスイッチなら何もしない。
    void SetSwitch(std::size_t index, std::int32_t value) override;
    PluginInfoViewModel ViewModel() const;

private:
    PluginSession& session_;
};

} // namespace rs::app
