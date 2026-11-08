// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>
namespace ctr {
// Keep ordinary scheduler jitter on the original timeline, but discard large
// timing debt rather than producing a burst of catch-up frames after a hitch.
struct FramePacer {
    using Clock = std::chrono::steady_clock;
    Clock::duration period;
    Clock::time_point deadline;
    unsigned resets = 0;
    Clock::time_point advance(Clock::time_point now) {
        deadline += period;
        if (now > deadline + period) { deadline = now; ++resets; }
        return deadline;
    }
    void resume(Clock::time_point now) { deadline = now; }
};
}
