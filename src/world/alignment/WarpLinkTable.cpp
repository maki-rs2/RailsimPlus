#include "WarpLinkTable.h"

#include <algorithm>

namespace rs::world {

namespace {

WarpNodeKey KeyOf(const WarpEndpoint& endpoint) {
    return WarpNodeKey{endpoint.sceneIndex, endpoint.node};
}

} // namespace

void WarpLinkTable::AddLink(const WarpEndpoint& endpointA, const WarpEndpoint& endpointB) {
    const WarpNodeKey keyA = KeyOf(endpointA);
    const WarpNodeKey keyB = KeyOf(endpointB);

    // 後勝ち上書き: どちらかの端点が既存リンクに属していたら、その相手側の
    // 逆引きエントリと links_ 内の該当エントリも合わせて壊す(片側だけ古い
    // 逆引きが残る不整合を防ぐ)。
    for (const WarpNodeKey& key : {keyA, keyB}) {
        std::unordered_map<WarpNodeKey, WarpEndpoint, WarpNodeKeyHash>::iterator it = lookup_.find(key);
        if (it == lookup_.end()) continue;
        const WarpNodeKey oldPartnerKey = KeyOf(it->second);
        lookup_.erase(it);
        lookup_.erase(oldPartnerKey);
        links_.erase(std::remove_if(links_.begin(), links_.end(),
                                     [&](const WarpLink& link) {
                                         return KeyOf(link.a) == key || KeyOf(link.b) == key ||
                                                KeyOf(link.a) == oldPartnerKey || KeyOf(link.b) == oldPartnerKey;
                                     }),
                     links_.end());
    }

    lookup_[keyA] = endpointB;
    lookup_[keyB] = endpointA;
    links_.push_back(WarpLink{endpointA, endpointB});
}

void WarpLinkTable::RemoveLink(const WarpEndpoint& endpointA, const WarpEndpoint& endpointB) {
    const WarpNodeKey keyA = KeyOf(endpointA);
    const WarpNodeKey keyB = KeyOf(endpointB);

    for (const WarpNodeKey& key : {keyA, keyB}) {
        std::unordered_map<WarpNodeKey, WarpEndpoint, WarpNodeKeyHash>::iterator it = lookup_.find(key);
        if (it == lookup_.end()) continue;
        const WarpNodeKey oldPartnerKey = KeyOf(it->second);
        lookup_.erase(it);
        lookup_.erase(oldPartnerKey);
        links_.erase(std::remove_if(links_.begin(), links_.end(),
                                     [&](const WarpLink& link) {
                                         return KeyOf(link.a) == key || KeyOf(link.b) == key ||
                                                KeyOf(link.a) == oldPartnerKey || KeyOf(link.b) == oldPartnerKey;
                                     }),
                     links_.end());
    }
}

std::optional<WarpEndpoint> WarpLinkTable::Find(size_t sceneIndex, const Guid& node) const {
    std::unordered_map<WarpNodeKey, WarpEndpoint, WarpNodeKeyHash>::const_iterator it =
        lookup_.find(WarpNodeKey{sceneIndex, node});
    if (it == lookup_.end()) return std::nullopt;
    return it->second;
}

void WarpLinkTable::Clear() {
    links_.clear();
    lookup_.clear();
}

} // namespace rs::world
