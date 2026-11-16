#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../world/alignment/Guid.h"
#include "ScenePicking.h"

namespace rs::app {

struct ScenePickItem {
    rs::world::Guid entityId = rs::world::NilGuid();
    ScreenPickCandidate candidate;
    bool isStaticInstanced = false;
    std::string folderUtf8;
    std::string definitionFile;
    std::vector<int32_t> switchOptions;
    bool signalModel = false;  // 信号のモデルの個体(静的メッシュの取得で別のキャッシュを引く)
};

} // namespace rs::app
