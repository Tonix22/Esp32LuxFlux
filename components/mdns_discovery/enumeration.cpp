#include "enumeration.hpp"
#include <algorithm>
#include <cctype>

namespace luxflux::discovery {

bool validDeviceId(const std::string &id)
{
    return id.size() == 12 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isxdigit(c) && !std::islower(c);
    });
}

int scientistIndex(const std::string &name)
{
    for (unsigned i = 0; i < kScientistCount; ++i)
        if (name == SCIENTISTS[i]) return static_cast<int>(i);
    return -1;
}

const char *firstAvailable(const std::vector<Peer> &peers, const std::string &self_id)
{
    for (const char *name : SCIENTISTS) {
        const bool occupied = std::any_of(peers.begin(), peers.end(), [&](const Peer &peer) {
            return peer.device_id != self_id && peer.logical_name == name &&
                   validDeviceId(peer.device_id);
        });
        if (!occupied) return name;
    }
    return nullptr;
}

bool mustYield(const std::string &self_id, bool self_active,
               const std::string &claimed_name, const std::vector<Peer> &peers)
{
    for (const Peer &peer : peers) {
        if (peer.device_id == self_id || peer.logical_name != claimed_name ||
            !validDeviceId(peer.device_id)) continue;
        // A provisional device has already missed this owner during its three
        // initial scans. Treat a late response as an unresolved collision so
        // a near-simultaneous claim cannot win merely by publishing "active"
        // a moment earlier. An established active device never yields to a
        // provisional newcomer; two active owners converge by MAC.
        if ((!self_active || peer.active) && peer.device_id < self_id) {
            return true;
        }
    }
    return false;
}

void mergePeers(std::vector<Peer> &destination, const std::vector<Peer> &source)
{
    for (const Peer &peer : source) {
        if (!validDeviceId(peer.device_id)) continue;
        auto existing = std::find_if(destination.begin(), destination.end(),
                                     [&](const Peer &item) { return item.device_id == peer.device_id; });
        if (existing == destination.end()) destination.push_back(peer);
        else if (peer.active || !existing->active) *existing = peer;
    }
}

std::vector<Peer> mergeDiscoveryAttempts(const std::array<std::vector<Peer>, kDiscoveryAttempts> &attempts)
{
    std::vector<Peer> merged;
    for (const auto &attempt : attempts) mergePeers(merged, attempt);
    return merged;
}

} // namespace luxflux::discovery
