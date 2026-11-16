#include "AsyncLoader.h"

#include <unordered_set>

#include "../../platform/thread/ParallelFor.h"
#include "TextureLoader.h"

namespace rs::render {

void PrefetchImages(const std::vector<std::string>& utf8Paths) {
    // 同一basePathの重複ジョブを除去(アセンブリメッシュは同一テクスチャが
    // 数百マテリアルに重複参照されるため、そのまま投げるとワーカー間の
    // ミューテックス競合が無駄に増える)。
    std::vector<std::string> distinctPaths;
    distinctPaths.reserve(utf8Paths.size());
    std::unordered_set<std::string> seen;
    seen.reserve(utf8Paths.size());
    for (const std::string& p : utf8Paths) {
        if (p.empty()) continue;
        if (seen.insert(p).second) distinctPaths.push_back(p);
    }
    if (distinctPaths.empty()) return;

    if (distinctPaths.size() == 1) {
        LoadImageRGBA(distinctPaths[0]); // 1件だけならスレッド起動コストが無駄
        return;
    }

    // 戻り値はImageCache(TextureLoader.cpp)に格納されるだけで使わない。
    // 後続の同期経路(TextureManager::Acquire等)が同じパスをGetする際に
    // キャッシュヒットする。
    rs::platform_thread::ParallelFor(distinctPaths.size(), [&distinctPaths](size_t i) {
        LoadImageRGBA(distinctPaths[i]);
    });
}

} // namespace rs::render
