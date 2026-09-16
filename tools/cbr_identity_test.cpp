// Apply the integration workflow patches first, then run:
// g++ -std=c++17 -Wall -Wextra -pedantic tools/cbr_identity_test.cpp -o /tmp/cbr_identity_test
// /tmp/cbr_identity_test
#include "../src/Network/CbrMatchIdentity.h"
#include <cassert>
#include <iostream>

int main()
{
    const uint64_t local = 76561198000000001ULL;
    const uint64_t remote = 76561198000000002ULL;
    const uint64_t spectator = 76561198000000003ULL;
    const uint64_t other = 76561198000000004ULL;
    using Members = std::vector<CbrMatchMember>;
    auto resolve = [&](const Members& m, bool ranked = false, bool ffa = false, int side = -1)
    { return ResolveCbrMatchIdentity(m, local, ranked, ffa, side); };

    // Enumeration order does not assign sides; the local player may be either side.
    for (unsigned int side = 0; side < 2; ++side)
    {
        const auto r = resolve({{remote, 7, 4, 1 - side}, {local, 7, 1, side}});
        assert(r.resolved && r.players[side].steamId == local);
        assert(r.players[1 - side].steamId == remote);
    }
    const auto room = resolve({{spectator, 7, 0, 2}, {other, 8, 1, 1},
        {remote, 7, 2, 1}, {local, 7, 3, 0}});
    assert(room.resolved && room.players[1].steamId == remote);
    assert(!resolve({{local, 7, 0, 0}}).resolved);
    assert(!resolve({{remote, 7, 0, 0}, {other, 7, 1, 1}}).resolved);
    assert(!resolve({{local, 7, 0, 2}, {remote, 7, 1, 0}, {other, 7, 2, 1}}).resolved);
    assert(!resolve({{local, 7, 0, 0}, {remote, 7, 1, 0}}).resolved);
    assert(!resolve({{local, 7, 0, 0}, {remote, 7, 1, 1}, {other, 7, 2, 1}}).resolved);
    assert(!resolve({{local, 7, 0, 0}, {local, 7, 1, 1}}).resolved);
    assert(!resolve({{local, 7, 0, 0}, {0, 7, 1, 1}}).resolved);

    // A zero match ID is accepted only in a two-person Ranked room with valid slots.
    const Members zero{{local, 0, 0, 1}, {remote, 0, 1, 0}};
    assert(resolve(zero, true).resolved);
    assert(!resolve(zero).resolved);
    assert(!resolve({{local, 0, 0, 0}, {remote, 0, 1, 1}, {spectator, 0, 2, 2}}, true).resolved);
    assert(!resolve({{local, 0, 0, 0}, {remote, 1, 1, 1}}, true).resolved);

    // FFA uses its separate side pointer, but still requires a unique same-match pair.
    const Members ffa{{other, 8, 0, 0}, {remote, 7, 1, 0}, {local, 7, 2, 0}};
    for (int side = 0; side < 2; ++side)
    {
        const auto r = resolve(ffa, false, true, side);
        assert(r.resolved && r.players[side].steamId == local);
        assert(r.players[1 - side].steamId == remote);
    }
    assert(!resolve(ffa, false, true, -1).resolved);
    assert(!resolve(ffa, false, true, 2).resolved);
    assert(!resolve({{local, 7, 0, 0}, {remote, 7, 1, 0}, {other, 7, 2, 0}}, false, true, 0).resolved);
    assert(!resolve(zero, false, true, 0).resolved);
    std::cout << "CBR identity resolver regression tests passed\n";
}
