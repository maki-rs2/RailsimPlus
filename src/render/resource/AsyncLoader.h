#pragma once

#include <string>
#include <vector>

namespace rs::render {

// テクスチャデコード(TextureLoader::LoadImageRGBA)をワーカースレッドで並列実行し、
// 内部のImageCache(パス+mtimeキー)を温める。呼び出しは全ジョブ完了までブロックする
// (フェンス/フレーム跨ぎの非同期状態機械は導入しない — 既存の同期アップロード経路
// [TextureManager::Acquire等]はそのままで、直前に呼ぶだけで実質のデコードコストを
// 並列化できる)。
// utf8Paths内の重複は呼び出し側で除去済みであることを前提にしない(内部でも
// ImageCacheのmutexで問題なく扱えるため、重複が残っていても正しく動く)。
void PrefetchImages(const std::vector<std::string>& utf8Paths);

} // namespace rs::render
