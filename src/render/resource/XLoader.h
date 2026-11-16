//
// XLoader.h - assimp 経由での .x 読込み
//
#pragma once

#include <string>
#include <vector>

#include "../mesh/Mesh.h"

namespace rs::legacy_x {

struct LoadResult {
    bool                     ok = false;
    rs::render::Mesh         mesh;
    std::vector<std::string> log; // UTF-8。読込ログ/警告
};

// assimpでファイルから読み込む。text/binary/tzip/bzip すべて対応。
// テクスチャ名は .x のあるディレクトリ基準で解決し、Material.texturePath に格納。
// 内部でパス(+更新時刻)キーの LRU キャッシュを持ち、同一 .x の再パースを避ける。
LoadResult LoadXFileAssimp(const std::wstring& path);

// メモリ上の UTF-8 テキストから読み込む(テスト用。baseDir はテクスチャ解決の基準)。
LoadResult LoadXFromTextAssimp(const std::string& utf8, const std::string& baseDirUtf8);

} // namespace rs::legacy_x
