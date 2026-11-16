#include "Rs2WarpImport.h"

#include <optional>
#include <utility>

namespace rs::legacy_layout {

using rs::world::Guid;
using rs::world::GuidIsNil;
using rs::world::WarpEndpoint;
using rs::world::WarpLinkTable;

namespace {

// connAddr を全Sceneから先頭順に探し、最初に見つかったSceneのAddressMapで解決する。
std::optional<std::pair<size_t, Guid>> ResolveConnectorAcrossScenes(
    const Rs2Address& connAddr, const std::vector<AddressMap>& sceneAddresses) {
    for (size_t i = 0; i < sceneAddresses.size(); ++i) {
        const Guid guid = sceneAddresses[i].ResolveConnector(connAddr);
        if (!GuidIsNil(guid)) return std::make_pair(i, guid);
    }
    return std::nullopt;
}

} // namespace

WarpLinkTable ImportWarpLinks(const std::vector<Rs2Warp>& warps,
                              const std::vector<AddressMap>& sceneAddresses,
                              std::vector<std::string>& warnings) {
    WarpLinkTable table;

    for (const Rs2Warp& w : warps) {
        const std::optional<std::pair<size_t, Guid>> epA =
            ResolveConnectorAcrossScenes(w.link0.connector, sceneAddresses);
        if (!epA) {
            warnings.push_back("Warp " + w.address + ": 端点 " + w.link0.connector + " が未解決、スキップ");
            continue;
        }
        const std::optional<std::pair<size_t, Guid>> epB =
            ResolveConnectorAcrossScenes(w.link1.connector, sceneAddresses);
        if (!epB) {
            warnings.push_back("Warp " + w.address + ": 端点 " + w.link1.connector + " が未解決、スキップ");
            continue;
        }

        if (epA->first == epB->first && epA->second == epB->second) {
            warnings.push_back("Warp " + w.address + ": 両端が同一端点、スキップ");
            continue;
        }

        // 既に別のwarpの端点として使われていたら、AddLinkが黙って後勝ち上書きする前に
        // 記録しておく(1本消えることをwarningsで可視化する)。
        if (table.Find(epA->first, epA->second).has_value()) {
            warnings.push_back("Warp " + w.address + ": 端点 " + w.link0.connector +
                                " は既に別のwarpに使われている(後勝ちで上書き)");
        }
        if (table.Find(epB->first, epB->second).has_value()) {
            warnings.push_back("Warp " + w.address + ": 端点 " + w.link1.connector +
                                " は既に別のwarpに使われている(後勝ちで上書き)");
        }

        table.AddLink(WarpEndpoint{epA->first, epA->second}, WarpEndpoint{epB->first, epB->second});
    }

    return table;
}

} // namespace rs::legacy_layout
