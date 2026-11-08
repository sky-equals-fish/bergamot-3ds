// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
#include <vector>
#include <charconv>

int main(int argc, char** argv) {
    try {
        if (argc < 3 || argc > 6) {
            std::cerr << "Usage: ctr-probe game.3dsx isolated-data-directory [software|vulkan] [scale 1-4] [frame.ppm]\n";
            return 2;
        }
        const bool vulkan = argc >= 4 && std::string(argv[3]) == "vulkan";
        unsigned scale = 1;
        if (argc >= 5) {
            const std::string value(argv[4]);
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), scale);
            if (error != std::errc{} || end != value.data() + value.size()) throw std::runtime_error("Invalid scale");
        }
        ctr::Runtime runtime(argv[2], vulkan, scale);
        std::vector<std::uint8_t> last_frame;
        unsigned frame_width = 0, frame_height = 0;
        runtime.set_frame_sink([&](const void* data, unsigned w, unsigned h, std::size_t pitch) {
            frame_width = w; frame_height = h;
            if (argc < 6) return;
            last_frame.resize(static_cast<std::size_t>(w) * h * 3);
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x)
                for (unsigned c = 0; c < 3; ++c)
                    last_frame[(static_cast<std::size_t>(y) * w + x) * 3 + c] = bytes[y * pitch + x * 4 + 2 - c];
        });
        runtime.load(argv[1]);
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 120 && !runtime.stopped(); ++i) runtime.step();
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto stats = runtime.performance();
        if (argc >= 6 && !last_frame.empty()) {
            std::ofstream file(argv[5], std::ios::binary);
            file << "P6\n" << frame_width << ' ' << frame_height << "\n255\n";
            file.write(reinterpret_cast<const char*>(last_frame.data()), static_cast<std::streamsize>(last_frame.size()));
            if (!file) throw std::runtime_error("Could not write frame");
        }
        std::cout << "{\"probe_seen\":" << (runtime.probe_seen() ? "true" : "false")
                  << ",\"video_callbacks\":" << runtime.frames()
                  << ",\"seconds\":" << elapsed
                  << ",\"width\":" << frame_width << ",\"height\":" << frame_height
                  << ",\"gsp_game_fps\":" << stats.game_fps << ",\"emulation_speed\":" << stats.emulation_speed
                  << ",\"backend\":\"" << (vulkan ? "vulkan-moltenvk-readback" : "software")
                  << "\",\"jit\":false,\"gameplay_benchmark\":false}\n";
        const bool requires_marker = std::filesystem::path(argv[1]).filename() == "arm11-probe.3dsx";
        return (!requires_marker || runtime.probe_seen()) && runtime.frames() > 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
