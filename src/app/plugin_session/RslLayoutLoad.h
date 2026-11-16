// RslLayoutLoad.h - .rsl レイアウト読込の公開インターフェース
#pragma once

#include <cstddef>
#include <string>

#include "RslLayoutSerialize.h"
#include "../../world/alignment/Guid.h"

namespace rs::app {

struct NormalizedLayout;

LayoutActionResult DecodeRsl(const std::string& pathUtf8, const RslPersistenceRefs& refs,
                        NormalizedLayout& normalized);

}  // namespace rs::app
