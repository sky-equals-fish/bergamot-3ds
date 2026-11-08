// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <libretro.h>
#include "vulkan_backend.hpp"
#include <exception>
#include <memory>
#include <stdexcept>
#include "citra_libretro/ctr_bridge.h"

namespace ctr {
// One core instance per process and one title per Runtime. Construct, load, step, unload and destroy on
// the same emulation thread. Input setters may be called from the UI thread.
class Runtime final {
public:
    using FrameSink = std::function<void(const void*, unsigned, unsigned, std::size_t)>;
    using AudioSink = std::function<void(const std::int16_t*, std::size_t)>;
    using HandoffSync = VulkanBackend::HandoffSync;
    explicit Runtime(const std::filesystem::path& data_root, bool vulkan = false, unsigned scale = 1);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    void load(const std::filesystem::path& game);
    ctr_content_info inspect(const std::filesystem::path& file) const;
    std::filesystem::path install_package(const std::filesystem::path& file);
    ctr_perf_info performance() const;
    void step();
    void unload();
    void set_button(unsigned button, bool down);
    void set_touch(std::int16_t x, std::int16_t y, bool down);
    void set_circle_pad(std::int16_t x, std::int16_t y) { circle_x_ = x; circle_y_ = y; }
    void set_c_stick(std::int16_t x, std::int16_t y) { cstick_x_ = x; cstick_y_ = y; }
    void set_frame_sink(FrameSink sink) { frame_sink_ = std::move(sink); }
    void set_audio_sink(AudioSink sink) { audio_sink_ = std::move(sink); }
    void set_gpu_presenter(VulkanBackend::Presenter presenter) {
        if (vulkan_) vulkan_->set_handoff_sync(HandoffSync::Serialized);
        gpu_presenter_ = std::move(presenter); gpu_async_presenter_ = {};
    }
    // QueueOrdered requires a presenter committing on MoltenVK's command queue
    // (CTRExportCommandQueue from gpu_queue()); see VulkanBackend::HandoffSync.
    void set_gpu_async_presenter(VulkanBackend::AsyncPresenter presenter, HandoffSync sync = HandoffSync::Serialized) {
        if (vulkan_) vulkan_->set_handoff_sync(sync);
        else if (sync != HandoffSync::Serialized) throw std::logic_error("Queue-ordered presentation requires the Vulkan runtime");
        gpu_async_presenter_ = std::move(presenter); gpu_presenter_ = {};
    }
    HandoffSync handoff_sync() const { return vulkan_ ? vulkan_->handoff_sync() : HandoffSync::Serialized; }
    // The core's Vulkan queue; valid after load() until unload().
    VkQueue gpu_queue() const { return vulkan_ ? vulkan_->queue() : VK_NULL_HANDLE; }
    void drain_presentation() { if (vulkan_) { vulkan_->drain(); vulkan_->check_async_error(); } }
    VulkanBackend::HandoffStats handoff_stats() const { return vulkan_ ? vulkan_->handoff_stats() : VulkanBackend::HandoffStats{}; }
    bool probe_seen() const { return probe_seen_.load(); }
    std::uint64_t frames() const { return frames_; }
    bool stopped() const { return shutdown_; }
    std::string message() const;
    retro_system_av_info av_info() const;
private:
    static bool environment(unsigned cmd, void* data) noexcept;
    static void log(enum retro_log_level level, const char* fmt, ...) noexcept;
    static void video(const void* data, unsigned width, unsigned height, std::size_t pitch) noexcept;
    static std::size_t audio(const std::int16_t* data, std::size_t frames) noexcept;
    static void audio_sample(std::int16_t left, std::int16_t right) noexcept;
    static std::int16_t input(unsigned port, unsigned device, unsigned index, unsigned id);
    static std::atomic<Runtime*> active_;
    void record_callback_error() noexcept;
    void rethrow_callback_error() const;
    mutable std::mutex callback_error_mutex_;
    std::string system_dir_, save_dir_;
    std::map<std::string, std::string> options_;
    mutable std::mutex message_mutex_;
    std::string message_;
    std::atomic_uint32_t buttons_{0};
    std::atomic_int16_t touch_x_{0}, touch_y_{0};
    std::atomic_int16_t circle_x_{0}, circle_y_{0};
    std::atomic_int16_t cstick_x_{0}, cstick_y_{0};
    std::atomic_bool touching_{false}, probe_seen_{false};
    FrameSink frame_sink_;
    AudioSink audio_sink_;
    VulkanBackend::Presenter gpu_presenter_;
    VulkanBackend::AsyncPresenter gpu_async_presenter_;
    std::unique_ptr<VulkanBackend> vulkan_;
    retro_hw_render_callback hardware_{};
    const retro_hw_render_context_negotiation_interface_vulkan* negotiation_ = nullptr;
    std::exception_ptr callback_error_;
    bool loaded_ = false, initialized_ = false, shutdown_ = false;
    bool attempted_load_ = false;
    std::uint64_t frames_ = 0;
};
}
