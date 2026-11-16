#include "WarpLinkTableSerialize.h"

#include <spdlog/spdlog.h>

#include "GuidJson.h"

namespace rs::world {

nlohmann::json WarpLinkTableToJson(const WarpLinkTable& table) {
    nlohmann::json result = nlohmann::json::array();
    for (const WarpLink& link : table.Links()) {
        result.push_back({
            { "sceneA", link.a.sceneIndex },
            { "nodeA", GuidToJson(link.a.node) },
            { "sceneB", link.b.sceneIndex },
            { "nodeB", GuidToJson(link.b.node) },
        });
    }
    return result;
}

WarpLinkTable WarpLinkTableFromJson(const nlohmann::json& json, size_t sceneCount) {
    WarpLinkTable result;
    if (!json.is_array()) {
        spdlog::warn("[warp_link_table_serialize] warps is not an array; skipping it.");
        return result;
    }

    for (const nlohmann::json& item : json) {
        try {
            if (!item.is_object()) {
                spdlog::warn("[warp_link_table_serialize] warp entry is not an object; skipping it.");
                continue;
            }

            const WarpEndpoint endpointA = {
                item.at("sceneA").get<size_t>(),
                GuidFromJson(item.at("nodeA")),
            };
            const WarpEndpoint endpointB = {
                item.at("sceneB").get<size_t>(),
                GuidFromJson(item.at("nodeB")),
            };
            if (endpointA.sceneIndex >= sceneCount || endpointB.sceneIndex >= sceneCount) {
                spdlog::warn("[warp_link_table_serialize] warp entry references a scene outside the layout; "
                             "skipping it.");
                continue;
            }
            result.AddLink(endpointA, endpointB);
        } catch (const nlohmann::json::exception&) {
            spdlog::warn("[warp_link_table_serialize] warp entry is invalid; skipping it.");
        }
    }
    return result;
}

}  // namespace rs::world
