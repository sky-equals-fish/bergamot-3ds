// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "metal_presenter.h"
#include <fstream>
#include <iostream>
#include <charconv>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>

unsigned parse_count(const char* text, unsigned maximum) {
    const std::string value(text);
    unsigned result = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result == 0 || result > maximum)
        throw std::runtime_error("Invalid frame count or scale");
    return result;
}

int main(int argc, char** argv) {
    @autoreleasepool {
        try {
            if (argc < 4 || argc > 7) throw std::runtime_error("Usage: ctr-metal-probe game data-directory output.ppm [frames] [scale] [--sync|--deferred|--queue-ordered]");
            const bool interactive = argc >= 5 && std::string(argv[4]) == "--interactive";
            const unsigned frame_limit = interactive ? 36000 : (argc >= 5 ? parse_count(argv[4], 36000) : 120);
            const unsigned scale = argc >= 6 ? parse_count(argv[5], 4) : 1;
            const std::string mode = argc == 7 ? argv[6] : "--sync";
            if (mode != "--sync" && mode != "--deferred" && mode != "--queue-ordered") throw std::runtime_error("Invalid presentation mode");
            const bool queue_ordered = mode == "--queue-ordered";
            const bool deferred = queue_ordered || mode == "--deferred";
            const char* mode_name = queue_ordered ? "queue-ordered" : deferred ? "deferred" : "sync";
            ctr::Runtime runtime(argv[2], true, scale);
            CTRMetalPresenter* presenter = nil;
            id<MTLTexture> output = nil;
            runtime.set_gpu_async_presenter([&](VkDevice device, VkImage image, unsigned w, unsigned h) -> ctr::VulkanBackend::Completion {
                @autoreleasepool {
                    id<MTLTexture> texture = CTRExportTexture(device, image);
                    if (!presenter) {
                        // Queue-ordered presentation draws on MoltenVK's own queue so
                        // Metal orders it after the core's rendering without CPU waits.
                        presenter = queue_ordered
                            ? [[CTRMetalPresenter alloc] initWithCommandQueue:CTRExportCommandQueue(device, runtime.gpu_queue())]
                            : [[CTRMetalPresenter alloc] initWithDevice:texture.device];
                    }
                    if (!output || output.width != w || output.height != h) {
                        auto* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
                        descriptor.storageMode = MTLStorageModeShared;
                        descriptor.usage = MTLTextureUsageRenderTarget;
                        output = [texture.device newTextureWithDescriptor:descriptor];
                    }
                    if (deferred) {
                        id<MTLCommandBuffer> command = [presenter enqueueDraw:texture to:output drawable:nil];
                        return [command] { CTRWaitForPresentation(command); };
                    }
                    [presenter draw:texture to:output drawable:nil];
                    return {};
                }
            }, queue_ordered ? ctr::Runtime::HandoffSync::QueueOrdered : ctr::Runtime::HandoffSync::Serialized);
            runtime.load(argv[1]);
            std::ofstream metrics(std::string(argv[3]) + ".metrics.jsonl");
            if (!metrics) throw std::runtime_error("Could not create metrics file");
            std::vector<double> frame_times;
            auto capture = [&] {
                runtime.drain_presentation();
                if (!output) throw std::runtime_error("No Metal output");
                std::vector<std::uint8_t> bytes(output.width * output.height * 4);
                [output getBytes:bytes.data() bytesPerRow:output.width * 4 fromRegion:MTLRegionMake2D(0, 0, output.width, output.height) mipmapLevel:0];
                std::ofstream file(argv[3], std::ios::binary);
                file << "P6\n" << output.width << ' ' << output.height << "\n255\n";
                for (std::size_t i = 0; i < bytes.size(); i += 4) {
                    file.put(static_cast<char>(bytes[i + 2])); file.put(static_cast<char>(bytes[i + 1])); file.put(static_cast<char>(bytes[i]));
                }
                if (!file) throw std::runtime_error("Could not write output image");
            };
            unsigned pending_steps = 0;
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < frame_limit && !runtime.stopped(); ++i) {
                if (interactive && pending_steps == 0) {
                    if (output) capture();
                    std::cout << "Paused at step " << i << ". Commands: step N, button ID 0|1, quit\n" << std::flush;
                    std::string line;
                    while (std::getline(std::cin, line)) {
                        std::istringstream command(line);
                        std::string verb;
                        command >> verb;
                        if (verb == "quit") break;
                        if (verb == "touch") {
                            int x = 0, y = 0, pressed = 0;
                            if (command >> x >> y >> pressed && x >= -32767 && x <= 32767 &&
                                y >= -32767 && y <= 32767 && (pressed == 0 || pressed == 1))
                                runtime.set_touch(static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), pressed != 0);
                            else std::cout << "Usage: touch x y 0|1 (coordinates -32767..32767)\n" << std::flush;
                            continue;
                        }
                        if (verb == "save" || verb == "restore") {
                            std::string path;
                            command >> path;
                            if (path.empty()) { std::cout << "Missing snapshot path\n" << std::flush; continue; }
                            const auto title = runtime.inspect(argv[1]).title_id;
                            if (verb == "save") {
                                const auto size = retro_serialize_size();
                                if (!size || size > 256 * 1024 * 1024) throw std::runtime_error("Invalid snapshot size");
                                std::vector<char> bytes(size);
                                if (!retro_serialize(bytes.data(), bytes.size())) throw std::runtime_error("Snapshot failed");
                                std::ofstream snapshot(path, std::ios::binary);
                                snapshot.write("CTRSTATE", 8);
                                snapshot.write(reinterpret_cast<const char*>(&title), sizeof(title));
                                snapshot.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                                if (!snapshot) throw std::runtime_error("Snapshot write failed");
                            } else {
                                std::ifstream snapshot(path, std::ios::binary | std::ios::ate);
                                const auto size = snapshot.tellg();
                                if (size <= 16 || size > 256 * 1024 * 1024) throw std::runtime_error("Invalid snapshot file");
                                snapshot.seekg(0);
                                char magic[8]; std::uint64_t saved_title = 0;
                                snapshot.read(magic, 8);
                                snapshot.read(reinterpret_cast<char*>(&saved_title), sizeof(saved_title));
                                if (std::string(magic, 8) != "CTRSTATE" || saved_title != title)
                                    throw std::runtime_error("Snapshot belongs to another title or format");
                                std::vector<char> bytes(static_cast<std::size_t>(size) - 16);
                                snapshot.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                                if (!snapshot || !retro_unserialize(bytes.data(), bytes.size())) throw std::runtime_error("Snapshot restore failed");
                            }
                            std::cout << "Snapshot " << verb << " complete\n" << std::flush;
                            continue;
                        }
                        unsigned value = 0, down = 0;
                        if (verb == "step" && command >> value && value > 0 && value <= 36000) {
                            pending_steps = value;
                            break;
                        }
                        if (verb == "read") {
                            unsigned address = 0, count = 0;
                            if (!(command >> std::hex >> address >> std::dec >> count) || !count || count > 256) {
                                std::cout << "Usage: read hex-address decimal-count (1-256)\n" << std::flush;
                                continue;
                            }
                            std::vector<unsigned char> bytes(count);
                            const auto title = runtime.inspect(argv[1]).title_id;
                            const int result = ctr_read_guest_memory(title, address, bytes.data(), count);
                            if (result) std::cout << "Read failed: " << result << '\n';
                            else {
                                std::cout << std::hex << std::setfill('0');
                                for (const auto byte : bytes) std::cout << std::setw(2) << unsigned(byte) << ' ';
                                std::cout << std::dec << '\n';
                            }
                            std::cout << std::flush;
                            continue;
                        }
                        if (verb == "button" && command >> value >> down && value < 16 && down <= 1)
                            runtime.set_button(value, down != 0);
                        else std::cout << "Invalid command\n" << std::flush;
                    }
                    if (!pending_steps) break;
                    // Exclude human inspection pauses from the next core timing sample.
                    (void)runtime.performance();
                }
                const auto before = std::chrono::steady_clock::now();
                runtime.step();
                if (interactive) --pending_steps;
                frame_times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count());
                if ((i + 1) % 60 == 0) {
                    const auto stats = runtime.performance();
                    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    metrics << "{\"step\":" << i + 1 << ",\"seconds\":" << seconds
                            << ",\"game_fps\":" << stats.game_fps << ",\"system_fps\":" << stats.system_fps
                            << ",\"emulation_speed\":" << stats.emulation_speed
                            << ",\"gpu_seconds\":" << stats.gpu_seconds << ",\"swap_seconds\":" << stats.swap_seconds << "}\n";
                    metrics.flush();
                }
            }
            if (!output || !runtime.frames() || frame_times.empty()) throw std::runtime_error("No Metal output");
            capture();
            const auto handoff = runtime.handoff_stats();
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::sort(frame_times.begin(), frame_times.end());
            const auto p95 = frame_times[static_cast<std::size_t>((frame_times.size() - 1) * 0.95)];
            std::cout << "{\"metal_frames\":" << runtime.frames() << ",\"step_p95_ms\":" << p95
                      << ",\"resolution_scale\":" << scale
                      << ",\"deferred_presentation\":" << (deferred ? "true" : "false")
                      << ",\"presentation_mode\":\"" << mode_name << "\""
                      << ",\"image_reuse_waits\":" << handoff.reuse_waits
                      << ",\"elapsed_seconds\":" << elapsed
                      << ",\"producer_wait_ms\":" << handoff.producer_wait_ms
                      << ",\"presenter_ms\":" << handoff.presenter_ms
                      << ",\"consumer_wait_ms\":" << handoff.consumer_wait_ms
                      << ",\"benchmark_type\":\"mac-offscreen-no-jit\",\"interactive\":" << (interactive ? "true" : "false")
                      << ",\"cpu_readback_at_interactive_pauses\":" << (interactive ? "true" : "false")
                      << ",\"device_gameplay_verified\":false}\n";
        } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    }
}
