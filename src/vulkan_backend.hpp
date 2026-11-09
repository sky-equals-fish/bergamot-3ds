// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <libretro_vulkan.h>
#include <cstdint>
#include <mutex>
#include <vector>
#include <functional>
#include <exception>

namespace ctr {
// Vulkan/MoltenVK host with synchronous readback and zero-copy Metal presentation.
// Two handoff modes: Serialized (default) and QueueOrdered. A "deferred" host is Serialized with a
// presenter that returns a completion, drained at the core's image-reuse boundary instead of at submit.
class VulkanBackend final {
public:
    // How the core's Vulkan rendering and the host's Metal presentation are ordered.
    enum class HandoffSync {
        // The CPU waits for the core's rendering before presenting, and finishes
        // the presentation before the core reuses its output image.
        Serialized,
        // The presenter commits onto MoltenVK's own MTLCommandQueue (see
        // CTRExportCommandQueue), so Metal's in-order queue execution orders
        // rendering, presentation and image reuse on the GPU. The CPU waits move
        // one presented frame back and only bound run-ahead; they no longer
        // serialize the emulation thread with the GPU every frame.
        QueueOrdered,
    };
    VulkanBackend() = default;
    ~VulkanBackend();
    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;
    void initialize(const retro_hw_render_context_negotiation_interface_vulkan& negotiation);
    retro_hw_render_interface_vulkan* interface() { return &interface_; }
    const std::vector<std::uint8_t>& readback(unsigned width, unsigned height);
    using Presenter = std::function<void(VkDevice, VkImage, unsigned, unsigned)>;
    using Completion = std::function<void()>;
    using AsyncPresenter = std::function<Completion(VkDevice, VkImage, unsigned, unsigned)>;
    struct HandoffStats {
        std::uint64_t frames = 0;
        // CPU waits performed at the core's output-image reuse boundary.
        std::uint64_t reuse_waits = 0;
        double producer_wait_ms = 0, presenter_ms = 0, consumer_wait_ms = 0;
    };
    void present(const Presenter& presenter, unsigned width, unsigned height);
    void present_async(const AsyncPresenter& presenter, unsigned width, unsigned height);
    // Completion must finish all source-image reads before returning or throwing.
    // Retires the pending presentation and the core's render fence.
    void drain();
    void check_async_error() const;
    HandoffStats handoff_stats() const { return stats_; }
    // QueueOrdered requires MoltenVK to commit submissions on the calling thread;
    // the setter rejects it otherwise. Takes effect at the next presentation.
    void set_handoff_sync(HandoffSync sync);
    HandoffSync handoff_sync() const { return sync_; }
    // Valid after initialize(). Hosts export MoltenVK's MTLCommandQueue from it.
    VkQueue queue() const { return context_.queue; }
    VkDevice device() const { return context_.device; }
    static bool queue_submits_are_synchronous();
private:
    Completion pending_completion_;
    std::exception_ptr async_error_;
    HandoffStats stats_;
    HandoffSync sync_ = HandoffSync::Serialized;
    void ensure_buffer(VkDeviceSize size);
    void settle_render_fence();
    retro_hw_render_interface_vulkan interface_{};
    VkInstance instance_{};
    retro_vulkan_context context_{};
    VkCommandPool pool_{};
    VkCommandBuffer command_{};
    VkFence fence_{};
    bool fence_pending_ = false;
    VkBuffer buffer_{};
    VkDeviceMemory memory_{};
    VkDeviceSize capacity_{};
    retro_vulkan_image image_{};
    bool image_valid_ = false;
    std::vector<VkSemaphore> waits_;
    // Keep submission scratch capacity across frames; typical handoff waits on one semaphore.
    std::vector<VkPipelineStageFlags> wait_stages_;
    std::vector<VkCommandBuffer> pending_commands_;
    VkSemaphore signal_{};
    std::vector<std::uint8_t> pixels_;
    std::mutex queue_mutex_;
};
}
