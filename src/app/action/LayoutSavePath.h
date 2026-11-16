//
// LayoutSavePath.h - レイアウト保存先パスの解決
//
#pragma once

#include <filesystem>
#include <string>

namespace rs::app {

std::filesystem::path ResolveLayoutSavePath(const std::string& inputUtf8,
                                            const std::filesystem::path& saveDirectory);

} // namespace rs::app
