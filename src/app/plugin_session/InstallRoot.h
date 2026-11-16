#pragma once

#include <filesystem>

namespace rs::app {

// レイアウトおよびテンプレートファイルは、インストールルート直下の専用フォルダに配置する。
// そのため、ファイルの祖父ディレクトリをインストールルートとして扱う。
inline std::filesystem::path ResolveInstallRootFromFilePath(const std::filesystem::path& filePath) {
    return filePath.parent_path().parent_path();
}

}  // namespace rs::app
