// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_pacer.hpp"
#include <stdexcept>
#include <iostream>
using namespace std::chrono_literals;
int main() {
    using Clock=ctr::FramePacer::Clock;
    const auto origin=Clock::time_point{};
    ctr::FramePacer pacer{16ms,origin};
    if(pacer.advance(origin+5ms)!=origin+16ms) throw std::runtime_error("Normal deadline drift");
    if(pacer.advance(origin+33ms)!=origin+32ms || pacer.resets) throw std::runtime_error("Small jitter changed cadence");
    if(pacer.advance(origin+95ms)!=origin+95ms || pacer.resets!=1) throw std::runtime_error("Stall retained catch-up debt");
    if(pacer.advance(origin+100ms)!=origin+111ms) throw std::runtime_error("Post-stall burst");
    pacer.resume(origin+5s);
    if(pacer.advance(origin+5s+3ms)!=origin+5s+16ms) throw std::runtime_error("Pause retained timing debt");
    std::cout<<"frame_pacer_pass: normal cadence, jitter, hitch recovery and resume\n";
}
