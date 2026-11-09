// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <array>
#include <cstring>

namespace ctr {
std::atomic<Runtime*> Runtime::active_{nullptr};
Runtime::Runtime(const std::filesystem::path& root, bool vulkan, unsigned scale) {
    if (active_) throw std::logic_error("Only one runtime may own the core");
    system_dir_ = (std::filesystem::absolute(root) / "system").string();
    save_dir_ = (std::filesystem::absolute(root) / "saves").string();
    std::filesystem::create_directories(system_dir_);
    std::filesystem::create_directories(save_dir_);
    if (scale < 1 || scale > 4) throw std::invalid_argument("Resolution scale must be 1 through 4");
    if (vulkan) vulkan_ = std::make_unique<VulkanBackend>();
    options_ = {{"citra_graphics_api", vulkan ? "Vulkan" : "Software"}, {"citra_use_cpu_jit", "disabled"},
                {"citra_layout_option", "default"}, {"citra_swap_screen", "Top"},
                {"citra_analog_function", "c_stick"}, {"citra_use_fastinterp", "enabled"}, {"citra_use_shader_jit", "disabled"},
                {"citra_resolution_factor", std::to_string(scale)}, {"citra_is_new_3ds", "New 3DS"}};
    if (retro_api_version() != RETRO_API_VERSION) throw std::runtime_error("Core ABI mismatch");
    Runtime* expected = nullptr;
    if (!active_.compare_exchange_strong(expected, this))
        throw std::logic_error("Only one runtime may own the core");
    try {
        retro_set_environment(environment);
        retro_set_video_refresh(video);
        retro_set_audio_sample_batch(audio);
        retro_set_audio_sample(audio_sample);
        retro_set_input_poll([] {});
        retro_set_input_state(input);
        retro_init();
        initialized_ = true;
        rethrow_callback_error();
    } catch (...) {
        if (initialized_) { retro_deinit(); initialized_ = false; }
        active_ = nullptr;
        throw;
    }
}
Runtime::~Runtime() {
    frame_sink_ = {}; audio_sink_ = {}; gpu_presenter_ = {}; gpu_async_presenter_ = {};
    try { unload(); } catch (...) {}
    if (initialized_) retro_deinit();
    active_ = nullptr;
}
void Runtime::load(const std::filesystem::path& game) {
    if (loaded_) throw std::logic_error("Unload the current title before loading another");
    if (attempted_load_) throw std::logic_error("Create a new Runtime session for another title");
    if (!std::filesystem::is_regular_file(game)) throw std::runtime_error("Game file does not exist");
    const auto path = std::filesystem::absolute(game).string();
    const retro_game_info info{path.c_str(), nullptr, 0, nullptr};
    attempted_load_ = true;
    ctr_set_shader_progress(shader_progress_ ? [](void* context, int stage, std::size_t current, std::size_t total) {
        try { static_cast<Runtime*>(context)->shader_progress_(stage, current, total); }
        catch (...) { static_cast<Runtime*>(context)->record_callback_error(); }
    } : nullptr, this);
    shutdown_ = false;
    frames_ = 0;
    probe_seen_ = false;
    try {
        loaded_ = retro_load_game(&info);
        rethrow_callback_error();
        if (!loaded_) throw std::runtime_error("Core rejected the game: " + message());
        if (vulkan_) {
            if (!negotiation_ || !hardware_.context_reset)
                throw std::runtime_error("Core did not negotiate Vulkan");
            vulkan_->initialize(*negotiation_);
            hardware_.context_reset();
            rethrow_callback_error();
        }
    } catch (...) {
        ctr_set_shader_progress(nullptr, nullptr);
        unload();
        throw;
    }
    ctr_set_shader_progress(nullptr, nullptr);
}
void Runtime::wait_for_shader_cache() {
    if (!loaded_) throw std::logic_error("No title loaded");
    if (const int status = ctr_wait_shader_cache())
        throw std::runtime_error("Shader preparation failed (" + std::to_string(status) + ")");
    rethrow_callback_error();
}
ctr_content_info Runtime::inspect(const std::filesystem::path& file) const {
    ctr_content_info info{};
    const auto status = ctr_inspect_content(file.c_str(), &info);
    if (status) throw std::runtime_error("Could not inspect 3DS title metadata (" + std::to_string(status) + ")");
    return info;
}
std::filesystem::path Runtime::install_package(const std::filesystem::path& file) {
    if (attempted_load_) throw std::logic_error("Install packages before starting a title");
    std::array<char, 4096> path{};
    const auto status = ctr_install_cia(file.c_str(), path.data(), path.size());
    if (status == 5) throw std::runtime_error("This package contains encrypted content; use your decrypted dump");
    if (status) throw std::runtime_error("Package installation failed (" + std::to_string(status) + ")");
    return path.data();
}
ctr_perf_info Runtime::performance() const {
    ctr_perf_info info{};
    if (ctr_sample_performance(&info)) throw std::logic_error("Performance requires a running title");
    return info;
}
void Runtime::step() {
    if (!loaded_) throw std::logic_error("No title loaded");
    rethrow_callback_error();
    if (vulkan_) vulkan_->check_async_error();
    if (!shutdown_) retro_run();
    rethrow_callback_error();
    if (vulkan_) vulkan_->check_async_error();
}
void Runtime::unload() {
    std::exception_ptr error;
    try { drain_presentation(); } catch (...) { error = std::current_exception(); }
    if (loaded_) { retro_unload_game(); loaded_ = false; }
    if (error) std::rethrow_exception(error);
}
retro_system_av_info Runtime::av_info() const {
    retro_system_av_info info{};
    retro_get_system_av_info(&info);
    return info;
}
std::string Runtime::message() const {
    const std::lock_guard lock(message_mutex_);
    return message_;
}
void Runtime::set_button(unsigned button, bool down) {
    if (button >= 16) return;
    const auto mask = std::uint32_t{1} << button;
    if (down) buttons_.fetch_or(mask); else buttons_.fetch_and(~mask);
}
void Runtime::set_touch(std::int16_t x, std::int16_t y, bool down) {
    touch_x_ = x; touch_y_ = y; touching_ = down;
}
void Runtime::record_callback_error() noexcept {
    const std::lock_guard lock(callback_error_mutex_);
    if (!callback_error_) callback_error_ = std::current_exception();
}
void Runtime::rethrow_callback_error() const {
    std::exception_ptr error;
    { const std::lock_guard lock(callback_error_mutex_); error = callback_error_; }
    if (error) std::rethrow_exception(error);
}
bool Runtime::environment(unsigned cmd, void* data) noexcept {
    auto* instance = active_.load();
    if (!instance) return false;
    auto& self = *instance;
    try {
        switch (cmd) {
        case RETRO_ENVIRONMENT_SET_HW_RENDER: {
            auto* hw = static_cast<retro_hw_render_callback*>(data);
            if (!self.vulkan_ || hw->context_type != RETRO_HW_CONTEXT_VULKAN) return false;
            self.hardware_ = *hw;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
            self.negotiation_ = static_cast<const retro_hw_render_context_negotiation_interface_vulkan*>(data);
            return self.vulkan_ && self.negotiation_->interface_type == RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN;
        case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
            if (!self.vulkan_) return false;
            *static_cast<const retro_hw_render_interface_vulkan**>(data) = self.vulkan_->interface();
            return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            static_cast<retro_log_callback*>(data)->log = log; return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            *static_cast<const char**>(data) = self.system_dir_.c_str(); return true;
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *static_cast<const char**>(data) = self.save_dir_.c_str(); return true;
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            *static_cast<unsigned*>(data) = 2; return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: {
            const auto* defs = static_cast<retro_core_options_v2*>(data)->definitions;
            for (auto* d = defs; d && d->key; ++d)
                if (d->default_value) self.options_.try_emplace(d->key, d->default_value);
            return true;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            auto* variable = static_cast<retro_variable*>(data);
            const auto it = self.options_.find(variable->key);
            variable->value = it == self.options_.end() ? nullptr : it->second.c_str();
            return variable->value != nullptr;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *static_cast<bool*>(data) = false; return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *static_cast<bool*>(data) = true; return true;
        case RETRO_ENVIRONMENT_GET_JIT_CAPABLE:
            *static_cast<bool*>(data) = false; return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            return *static_cast<retro_pixel_format*>(data) == RETRO_PIXEL_FORMAT_XRGB8888;
        case RETRO_ENVIRONMENT_SET_MESSAGE: {
            const auto* msg = static_cast<retro_message*>(data);
            const std::lock_guard lock(self.message_mutex_);
            self.message_ = msg->msg ? msg->msg : "";
            std::fprintf(stderr, "[core] %s\n", self.message_.c_str());
            return true;
        }
        case RETRO_ENVIRONMENT_SHUTDOWN: self.shutdown_ = true; return true;
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
            return true;
        default: return false; // Never claim an interface we do not implement.
        }
    } catch (...) { self.record_callback_error(); return false; }
}
void Runtime::log(enum retro_log_level level, const char* fmt, ...) noexcept {
    // Core logging is frequent; avoid allocating a vector and string per call.
    std::array<char, 16384> buffer{};
    va_list args; va_start(args, fmt);
    std::vsnprintf(buffer.data(), buffer.size(), fmt, args);
    va_end(args);
    auto* self = active_.load();
    if (self && std::strstr(buffer.data(), "CTR_RUNTIME_ARM11_PROBE_OK"))
        self->probe_seen_ = true;
    if (level >= RETRO_LOG_WARN || std::strstr(buffer.data(), "CTR_RUNTIME_"))
        std::fputs(buffer.data(), stderr);
}
void Runtime::video(const void* data, unsigned w, unsigned h, std::size_t pitch) noexcept {
    auto* instance = active_.load();
    if (!instance) return;
    auto& self = *instance;
    try {
        if (data == RETRO_HW_FRAME_BUFFER_VALID) {
            if (!self.vulkan_) throw std::logic_error("Unexpected hardware frame");
            if (self.gpu_async_presenter_) {
                self.vulkan_->present_async(self.gpu_async_presenter_, w, h);
                ++self.frames_;
                return;
            }
            if (self.gpu_presenter_) {
                self.vulkan_->present(self.gpu_presenter_, w, h);
                ++self.frames_;
                return;
            }
            const auto& pixels = self.vulkan_->readback(w, h);
            data = pixels.data(); pitch = static_cast<std::size_t>(w) * 4;
        }
        if (data && w && h) {
            ++self.frames_;
            if (self.frame_sink_) self.frame_sink_(data, w, h, pitch);
        }
    } catch (...) {
        self.record_callback_error();
    }
}
std::size_t Runtime::audio(const std::int16_t* data, std::size_t count) noexcept {
    auto* self = active_.load();
    try { if (self && self->audio_sink_) self->audio_sink_(data, count); }
    catch (...) { self->record_callback_error(); }
    return count;
}
void Runtime::audio_sample(std::int16_t left, std::int16_t right) noexcept {
    const std::int16_t samples[]{left, right};
    audio(samples, 1);
}
std::int16_t Runtime::input(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port != 0) return 0;
    const auto* instance = active_.load();
    if (!instance) return 0;
    const auto& self = *instance;
    if (device == RETRO_DEVICE_ANALOG && index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
        if (id == RETRO_DEVICE_ID_ANALOG_X) return self.circle_x_;
        if (id == RETRO_DEVICE_ID_ANALOG_Y) return self.circle_y_;
    }
    if (device == RETRO_DEVICE_ANALOG && index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
        if (id == RETRO_DEVICE_ID_ANALOG_X) return self.cstick_x_;
        if (id == RETRO_DEVICE_ID_ANALOG_Y) return self.cstick_y_;
    }
    if (device == RETRO_DEVICE_JOYPAD && id < 16)
        return (self.buttons_.load() >> id) & 1;
    if (device == RETRO_DEVICE_POINTER) {
        if (id == RETRO_DEVICE_ID_POINTER_X) return self.touch_x_;
        if (id == RETRO_DEVICE_ID_POINTER_Y) return self.touch_y_;
        if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return self.touching_;
    }
    return 0;
}
}
