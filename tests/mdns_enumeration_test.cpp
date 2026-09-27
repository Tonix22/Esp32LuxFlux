#include "enumeration.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>

using namespace luxflux::discovery;

static Peer peer(const char *id, const char *name, bool active = true)
{ return {id, name, active}; }

int main()
{
    constexpr const char *A = "001122334401";
    constexpr const char *B = "001122334402";
    constexpr const char *C = "001122334403";
    constexpr const char *D = "001122334404";

    // 1: A device on an empty network starts with the first name.
    assert(std::string(firstAvailable({}, A)) == "archimedes");

    // 2: Sequential joins occupy the exact registry order.
    std::vector<Peer> network;
    for (const char *id : {A, B, C}) {
        const char *name = firstAvailable(network, id);
        network.push_back(peer(id, name));
    }
    assert(network[0].logical_name == "archimedes");
    assert(network[1].logical_name == "bohr");
    assert(network[2].logical_name == "curie");

    // 3: A new device fills an established gap.
    network.erase(network.begin() + 1);
    assert(std::string(firstAvailable(network, D)) == "bohr");
    network.push_back(peer(D, "bohr"));

    // 4: A returning device discards its old claim and takes the next gap.
    assert(std::string(firstAvailable(network, B)) == "dirac");

    // 5 and 7: Simultaneous provisional claims resolve by factory MAC.
    const std::vector<Peer> higher_claim = {peer(B, "archimedes", false)};
    const std::vector<Peer> lower_claim = {peer(A, "archimedes", false)};
    assert(!mustYield(A, false, "archimedes", higher_claim));
    assert(mustYield(B, false, "archimedes", lower_claim));
    assert(std::string(firstAvailable({peer(A, "archimedes")}, B)) == "bohr");

    // An established owner is excluded during initial selection and does not
    // yield when a returning device presents a provisional claim.
    assert(std::string(firstAvailable({peer(B, "archimedes"), peer(D, "bohr")}, A)) == "curie");
    assert(!mustYield(D, true, "bohr", {peer(A, "bohr", false)}));
    // If a competing claim appears only during verification, MAC decides even
    // when the other device just published "active" first.
    assert(mustYield(D, false, "bohr", {peer(A, "bohr", true)}));
    // Two active owners after a partition converge by MAC.
    assert(mustYield(B, true, "archimedes", {peer(A, "archimedes")}));

    // 6: Three slow/incomplete query rounds merge valid observations.
    const std::array<std::vector<Peer>, kDiscoveryAttempts> rounds = {{
        {peer(A, "archimedes")}, {}, {peer(C, "curie")}
    }};
    const auto merged = mergeDiscoveryAttempts(rounds);
    assert(merged.size() == 2);
    assert(std::string(firstAvailable(merged, D)) == "bohr");

    // 8: The registry is exhausted without inventing a duplicate.
    std::vector<Peer> full;
    for (unsigned i = 0; i < kScientistCount; ++i) {
        char id[13];
        std::snprintf(id, sizeof(id), "%012X", i + 1);
        full.push_back({id, SCIENTISTS[i], true});
    }
    assert(firstAvailable(full, "FFFFFFFFFFFF") == nullptr);

    // 9: Reconnection starts fresh; a stale local identity is excluded.
    assert(std::string(firstAvailable({peer(A, "archimedes"), peer(D, "bohr")}, B)) == "curie");

    // 10: A missed periodic query is not evidence that a confirmed name is lost.
    assert(!mustYield(A, true, "archimedes", {}));

    // Invalid responses never occupy logical names or win collisions.
    assert(std::string(firstAvailable({peer("bad", "archimedes")}, A)) == "archimedes");
    std::cout << "mDNS enumeration simulations passed (10 scenarios)\n";
}
