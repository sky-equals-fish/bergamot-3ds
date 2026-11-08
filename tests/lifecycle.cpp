// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <chrono>
#include <thread>
#include <dlfcn.h>
#include <cstdlib>

// Capture the real adapter callback for deterministic fault injection. The
// homebrew probe produces no DSP audio, so waiting for it cannot test this path.
static retro_audio_sample_batch_t captured_audio = nullptr;
extern "C" void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    captured_audio = cb;
    auto real_setter = reinterpret_cast<decltype(&retro_set_audio_sample_batch)>(
        dlsym(RTLD_NEXT, "retro_set_audio_sample_batch"));
    if (!real_setter) std::abort();
    real_setter(cb);
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: ctr-lifecycle probe.3dsx isolated-data-directory");
        const std::filesystem::path root(argv[2]);
        std::filesystem::create_directories(root);
        const auto invalid = root / "invalid.3dsx";
        std::ofstream(invalid, std::ios::binary) << "not a 3DS title";
        {
            ctr::Runtime runtime(root / "reject");
            bool rejected = false;
            try { ctr::Runtime second(root / "second"); } catch (const std::logic_error&) { rejected = true; }
            require(rejected, "Accepted concurrent core ownership");
            rejected = false;
            try { runtime.load(root / "missing.3dsx"); } catch (const std::runtime_error&) { rejected = true; }
            require(rejected, "Accepted missing file");
            rejected = false;
            try { runtime.load(invalid); } catch (const std::runtime_error&) { rejected = true; }
            require(rejected, "Accepted invalid title");
        }
        {
            ctr::Runtime runtime(root / "audio-failure");
            runtime.load(argv[1]);
            runtime.set_audio_sink([](const std::int16_t*, std::size_t) {
                throw std::runtime_error("audio-sink-test");
            });
            require(captured_audio != nullptr, "Audio callback was not registered");
            const std::int16_t silence[2]{};
            // Must return through the C callback boundary without unwinding it.
            require(captured_audio(silence, 1) == 1, "Audio callback did not consume batch");
            bool surfaced = false;
            try { runtime.step(); }
            catch (const std::runtime_error& e) {
                require(std::string(e.what()) == "audio-sink-test", "Wrong callback error surfaced");
                surfaced = true;
            }
            require(surfaced, "Audio error was not surfaced by step");
            const auto frames = runtime.frames();
            surfaced = false;
            try { runtime.step(); } catch (const std::runtime_error&) { surfaced = true; }
            require(surfaced && runtime.frames() == frames, "Core advanced after callback failure");
        }
        {
            ctr::Runtime runtime(root / "deferred-completion", true);
            unsigned submitted = 0, completed = 0;
            runtime.set_gpu_async_presenter([&](VkDevice, VkImage, unsigned, unsigned) -> ctr::VulkanBackend::Completion {
                require(completed == submitted, "Core reused output before previous presentation completed");
                ++submitted;
                return [&] { ++completed; };
            });
            runtime.load(argv[1]);
            for (unsigned i = 0; i < 8; ++i) runtime.step();
            require(submitted == 8 && completed == 7, "Presentation completion was not deferred");
            require(runtime.handoff_stats().reuse_waits == 7, "Serialized handoff did not wait at every image reuse");
            runtime.unload();
            require(completed == 8, "Unload did not drain the last presentation");
            runtime.drain_presentation();
            require(completed == 8, "Presentation completion invoked more than once");
        }
        {
            ctr::Runtime runtime(root / "deferred-error", true);
            unsigned completed = 0;
            runtime.set_gpu_async_presenter([&](VkDevice, VkImage, unsigned, unsigned) -> ctr::VulkanBackend::Completion {
                return [&] {
                    ++completed;
                    throw std::runtime_error("presentation-completion-test");
                };
            });
            runtime.load(argv[1]);
            runtime.step();
            bool surfaced = false;
            try { runtime.step(); }
            catch (const std::runtime_error& e) {
                require(std::string(e.what()) == "presentation-completion-test", "Wrong completion error surfaced");
                surfaced = true;
            }
            require(surfaced && completed == 1, "Completion error escaped containment or was lost");
            const auto frames = runtime.frames();
            surfaced = false;
            try { runtime.step(); } catch (const std::runtime_error&) { surfaced = true; }
            require(surfaced && runtime.frames() == frames, "Core advanced after presentation failure");
            try { runtime.unload(); } catch (const std::runtime_error&) {}
            require(completed == 1, "Failed completion invoked more than once");
        }
        require(ctr::VulkanBackend::queue_submits_are_synchronous(), "Pinned MoltenVK does not submit synchronously");
        {
            ctr::Runtime runtime(root / "queue-ordered", true);
            unsigned submitted = 0, completed = 0;
            require(runtime.gpu_queue() == VK_NULL_HANDLE, "GPU queue reported before load");
            runtime.set_gpu_async_presenter([&](VkDevice, VkImage, unsigned, unsigned) -> ctr::VulkanBackend::Completion {
                // The previous presentation retires before the next handoff, so the
                // emulation thread never runs more than one presented frame ahead.
                require(completed == submitted, "Queue-ordered handoff ran more than one frame ahead");
                ++submitted;
                return [&] { ++completed; };
            }, ctr::Runtime::HandoffSync::QueueOrdered);
            require(runtime.handoff_sync() == ctr::Runtime::HandoffSync::QueueOrdered, "Handoff mode was not applied");
            runtime.load(argv[1]);
            require(runtime.gpu_queue() != VK_NULL_HANDLE, "No GPU queue after load");
            for (unsigned i = 0; i < 8; ++i) runtime.step();
            const auto stats = runtime.handoff_stats();
            require(submitted == 8 && completed == 7, "Queue-ordered completion was not retired one frame later");
            require(stats.frames == 8 && stats.reuse_waits == 0, "Queue-ordered handoff waited at image reuse");
            runtime.unload();
            require(completed == 8, "Unload did not drain the queue-ordered presentation");
            runtime.drain_presentation();
            require(completed == 8, "Queue-ordered completion invoked more than once");
        }
        {
            ctr::Runtime runtime(root / "queue-ordered-error", true);
            unsigned completed = 0;
            runtime.set_gpu_async_presenter([&](VkDevice, VkImage, unsigned, unsigned) -> ctr::VulkanBackend::Completion {
                return [&] {
                    ++completed;
                    throw std::runtime_error("queue-ordered-completion-test");
                };
            }, ctr::Runtime::HandoffSync::QueueOrdered);
            runtime.load(argv[1]);
            runtime.step();
            bool surfaced = false;
            try { runtime.step(); }
            catch (const std::runtime_error& e) {
                require(std::string(e.what()) == "queue-ordered-completion-test", "Wrong queue-ordered completion error surfaced");
                surfaced = true;
            }
            require(surfaced && completed == 1, "Queue-ordered completion error escaped containment or was lost");
            const auto frames = runtime.frames();
            surfaced = false;
            try { runtime.step(); } catch (const std::runtime_error&) { surfaced = true; }
            require(surfaced && runtime.frames() == frames, "Core advanced after queue-ordered presentation failure");
            try { runtime.unload(); } catch (const std::runtime_error&) {}
            require(completed == 1, "Failed queue-ordered completion invoked more than once");
        }
        for (unsigned cycle = 0; cycle < 3; ++cycle) {
            ctr::Runtime runtime(root / ("session-" + std::to_string(cycle)), cycle != 0);
            runtime.load(argv[1]);
            require(std::abs(runtime.av_info().timing.fps - 59.83122493939037) < 1e-8,
                    "Core refresh rate does not match guest clock");
            runtime.set_circle_pad(-32767, 32767);
            runtime.set_button(8, true);
            for (unsigned frame = 0; frame < 8; ++frame) runtime.step();
            // The core delivers logs on a separate thread. Keep the exact
            // frame-count assertion, but allow its probe marker to arrive.
            for (unsigned retry = 0; retry < 100 && !runtime.probe_seen(); ++retry)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            require(runtime.probe_seen() && runtime.frames() == 8, ("Session did not execute and produce frames: marker=" + std::to_string(runtime.probe_seen()) + " frames=" + std::to_string(runtime.frames())).c_str());
            runtime.unload();
            bool rejected = false;
            try { runtime.load(argv[1]); } catch (const std::logic_error&) { rejected = true; }
            require(rejected, "Accepted unsafe reuse of a core session");
        }
        std::cout << "{\"lifecycle_pass\":true,\"sequential_sessions\":3,\"malformed_file_rejected\":true,\"audio_callback_error_contained\":true,\"failed_session_stops_stepping\":true,\"queue_ordered_handoff_bounded\":true}\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
