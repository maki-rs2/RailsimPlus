//
// RunAnchor.h - 実体化済みedge×folderのrun位相ハンドオフ記録
//
// Pole/Pierの間隔位相はedge起点ではなくrun(そのfolderが連続して有効なedge列)起点で
// 決まるため、実体化のためにedgeからfolderを外すとrunが割れ、触っていない下流edgeの
// 配置が動いてしまう。これを防ぐため、走査は実体化済みedgeを通過させ(出力だけを
// EntryActiveAtで止める)、edgeを抜けるところで下流へ渡すアンカーをここに凍結した値から
// 復元する。凍結するので、実体化後にそのedgeをどう編集しても下流は不変になる。
//
#pragma once

#include <string>
#include <vector>

#include "../alignment/Guid.h"

namespace rs::world {

// 実体化済み edge×folder の位相ハンドオフ記録。個体リストとは独立に存在し、
// 全個体を削除しても残る。
struct RunAnchor {
    Guid        edge = NilGuid();
    std::string folderUtf8;
    double      anchorS = 0.0;  // 実体化時点で下流へ渡していたアンカーの、この edge
                                     // 始点からの弧長。**負値を取りうる**(edge が柱間隔より
                                     // 短くアンカーが自 edge の手前にある場合)。クランプ禁止。
    bool        hasAnchor = false;    // false = 渡すアンカーが無い(run 先頭など)
};

// runAnchors から (edge, folderUtf8) の記録を探す。無ければ nullptr。
inline const RunAnchor* FindRunAnchor(
    const std::vector<RunAnchor>& runAnchors, const Guid& edge,
    const std::string& folderUtf8) {
    for (const RunAnchor& section : runAnchors) {
        if (section.edge == edge && section.folderUtf8 == folderUtf8) return &section;
    }
    return nullptr;
}

} // namespace rs::world
