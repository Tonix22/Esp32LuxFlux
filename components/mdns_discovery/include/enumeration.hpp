#pragma once

#include <array>
#include <string>
#include <vector>

namespace luxflux::discovery {

static constexpr const char *SCIENTISTS[] = {
    "archimedes", "bohr", "curie", "dirac", "einstein", "faraday",
    "galileo", "hawking", "ibn-al-haytham", "joule", "kepler",
    "lovelace", "maxwell", "newton", "oppenheimer", "planck",
    "quetelet", "rutherford", "schrodinger", "tesla", "ulam",
    "volta", "watt", "xie", "yukawa", "zwicky"
};
static constexpr unsigned kScientistCount = sizeof(SCIENTISTS) / sizeof(SCIENTISTS[0]);
static constexpr unsigned kDiscoveryAttempts = 3;

enum class EnumerationState {
    INITIALIZING, DISCOVERING, SELECTING_NAME, CLAIMING_NAME, VERIFYING,
    ASSIGNED, CONFLICT, NO_AVAILABLE_NAME
};

struct Peer {
    std::string device_id;
    std::string logical_name;
    bool active = false;
};

bool validDeviceId(const std::string &id);
int scientistIndex(const std::string &name);
const char *firstAvailable(const std::vector<Peer> &peers, const std::string &self_id);
bool mustYield(const std::string &self_id, bool self_active,
               const std::string &claimed_name, const std::vector<Peer> &peers);
void mergePeers(std::vector<Peer> &destination, const std::vector<Peer> &source);
std::vector<Peer> mergeDiscoveryAttempts(const std::array<std::vector<Peer>, kDiscoveryAttempts> &attempts);

} // namespace luxflux::discovery
