#pragma once
#include "CbrIdentity.h"
#include <vector>
#include <cstddef>

// Commit valid recordings as one identity/character group. A rejected recording
// leaves the queue only after preserveFailure has durably saved it elsewhere.
// Storage exceptions propagate: uncommitted valid recordings stay retryable.
template<class Replay, class Load, class Convert, class Preserve, class Commit, class Retire>
void SaveCbrRecordingQueue(std::vector<Replay>& buffer, Load load, Convert convert,
    Preserve preserveFailure, Commit commit, Retire retire)
{
    while (!buffer.empty()) {
        // Erasing a rejected entry invalidates references into buffer.
        const auto id = buffer.front().getSteamId();
        const auto name = buffer.front().getPlayerName();
        const auto character = buffer.front().getFocusCharName();
        auto data = load(buffer.front());
        std::size_t accepted = 0;
        while (accepted < buffer.size()) {
            auto& replay = buffer[accepted];
            if (replay.getFocusCharName() != character ||
                !SameCbrIdentity(id, name, replay.getSteamId(), replay.getPlayerName())) break;
            const auto error = convert(replay, data);
            if (error.errorCount) {
                preserveFailure(replay, error); // May throw; do not erase on failure.
                buffer.erase(buffer.begin() + accepted);
                retire(0, 1);
            } else {
                ++accepted;
            }
        }
        if (accepted) {
            commit(data); // May throw; do not remove valid captures before commit.
            buffer.erase(buffer.begin(), buffer.begin() + accepted);
            retire(static_cast<int>(accepted), 0);
        }
    }
}
