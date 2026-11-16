// Overlay2DPassSetup.h - Overlay2DPass の設定読込
#pragma once

#include "Overlay2DPass.h"

namespace rs::render::overlay2d_setup {

// 埋め込みJSONから設定を読み、範囲へ丸める。読めなければ既定値。
Overlay2DPass::Settings ParseSettings();

} // namespace rs::render::overlay2d_setup
