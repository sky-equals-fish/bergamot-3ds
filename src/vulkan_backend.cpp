// SPDX-License-Identifier: GPL-3.0-or-later
#include "vulkan_backend.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <chrono>
#include <MoltenVK/mvk_private_api.h>

namespace ctr {
namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": VkResult " + std::to_string(result));
}
}
VulkanBackend::~VulkanBackend() {
    try { drain(); } catch (...) {}
    if (context_.device) {
        vkDeviceWaitIdle(context_.device);
        fence_pending_ = false;
        if (buffer_) vkDestroyBuffer(context_.device, buffer_, nullptr);
        if (memory_) vkFreeMemory(context_.device, memory_, nullptr);
        if (fence_) vkDestroyFence(context_.device, fence_, nullptr);
        if (pool_) vkDestroyCommandPool(context_.device, pool_, nullptr);
        vkDestroyDevice(context_.device, nullptr);
    }
    if (instance_) vkDestroyInstance(instance_, nullptr);
}
void VulkanBackend::initialize(const retro_hw_render_context_negotiation_interface_vulkan& n) {
    if (instance_) throw std::logic_error("Vulkan backend already initialized");
    if (!n.create_device) throw std::runtime_error("Core lacks supported Vulkan device negotiation");
    std::uint32_t extension_count = 0;
    check(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr), "enumerate instance extensions");
    std::vector<VkExtensionProperties> available(extension_count);
    check(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, available.data()), "read instance extensions");
    std::vector<const char*> extensions;
    bool portability = false;
    for (const auto& extension : available) {
        if (std::strcmp(extension.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
            extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME); portability = true;
        }
        if (std::strcmp(extension.extensionName, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) == 0)
            extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    }
    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.flags = portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0;
    instance_info.pApplicationInfo = n.get_application_info ? n.get_application_info() : nullptr;
    instance_info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    instance_info.ppEnabledExtensionNames = extensions.data();
    check(vkCreateInstance(&instance_info, nullptr, &instance_), "vkCreateInstance");
    std::uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "enumerate GPU count");
    if (!count) throw std::runtime_error("No Vulkan/Metal GPU");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "enumerate GPUs");
    const char* device_extensions[]{"VK_EXT_metal_objects"};
    if (!n.create_device(&context_, instance_, devices.front(), VK_NULL_HANDLE,
                         vkGetInstanceProcAddr, device_extensions, 1, nullptr, 0, nullptr))
        throw std::runtime_error("Core rejected the Metal GPU");
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = context_.queue_family_index;
    check(vkCreateCommandPool(context_.device, &pool_info, nullptr, &pool_), "create command pool");
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool_; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(context_.device, &alloc, &command_), "allocate command buffer");
    VkFenceCreateInfo fence_info{}; fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    check(vkCreateFence(context_.device, &fence_info, nullptr, &fence_), "create fence");
    interface_.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
    interface_.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    interface_.handle = this;
    interface_.instance = instance_; interface_.gpu = context_.gpu; interface_.device = context_.device;
    interface_.queue = context_.queue; interface_.queue_index = context_.queue_family_index;
    interface_.get_instance_proc_addr = vkGetInstanceProcAddr;
    interface_.get_device_proc_addr = vkGetDeviceProcAddr;
    interface_.get_sync_index = [](void*) -> std::uint32_t { return 0; };
    interface_.get_sync_index_mask = [](void*) -> std::uint32_t { return 1; };
    // The pinned core calls this before writing/recreating its shared output.
    // Never unwind a completion error through the C callback boundary.
    interface_.wait_sync_index = [](void* h) {
        auto& self = *static_cast<VulkanBackend*>(h);
        // QueueOrdered: the shared Metal queue already orders the core's next
        // write after the committed presentation, so no CPU wait at reuse.
        if (self.sync_ == HandoffSync::QueueOrdered) return;
        if (self.pending_completion_) ++self.stats_.reuse_waits;
        try { self.drain(); }
        catch (...) { if (!self.async_error_) self.async_error_ = std::current_exception(); }
    };
    interface_.lock_queue = [](void* h) { static_cast<VulkanBackend*>(h)->queue_mutex_.lock(); };
    interface_.unlock_queue = [](void* h) { static_cast<VulkanBackend*>(h)->queue_mutex_.unlock(); };
    interface_.set_image = [](void* h, const retro_vulkan_image* image, std::uint32_t count,
                              const VkSemaphore* semaphores, std::uint32_t family) {
        auto& self = *static_cast<VulkanBackend*>(h);
        self.image_valid_ = image && (family == VK_QUEUE_FAMILY_IGNORED || family == self.context_.queue_family_index);
        if (image) self.image_ = *image;
        self.waits_.clear();
        if (count) self.waits_.assign(semaphores, semaphores + count);
    };
    interface_.set_command_buffers = [](void* h, std::uint32_t count, const VkCommandBuffer* commands) {
        auto& self = *static_cast<VulkanBackend*>(h);
        if (count) self.pending_commands_.insert(self.pending_commands_.end(), commands, commands + count);
    };
    interface_.set_signal_semaphore = [](void* h, VkSemaphore signal) {
        static_cast<VulkanBackend*>(h)->signal_ = signal;
    };
}
void VulkanBackend::drain() {
    if (pending_completion_) {
        auto completion = std::move(pending_completion_);
        pending_completion_ = {};
        const auto start = std::chrono::steady_clock::now();
        completion();
        stats_.consumer_wait_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    settle_render_fence();
}
void VulkanBackend::settle_render_fence() {
    if (!fence_pending_) return;
    fence_pending_ = false;
    const auto start = std::chrono::steady_clock::now();
    check(vkWaitForFences(context_.device, 1, &fence_, VK_TRUE, std::numeric_limits<std::uint64_t>::max()), "wait Metal handoff");
    stats_.producer_wait_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    check(vkResetFences(context_.device, 1, &fence_), "reset Metal handoff");
}
void VulkanBackend::set_handoff_sync(HandoffSync sync) {
    if (sync == HandoffSync::QueueOrdered && !queue_submits_are_synchronous())
        throw std::runtime_error("Queue-ordered presentation needs MoltenVK synchronous queue submits");
    if (sync != sync_) drain();
    sync_ = sync;
}
bool VulkanBackend::queue_submits_are_synchronous() {
    // The statically linked MoltenVK answers for its own configuration; a size
    // mismatch means a different MoltenVK than the pinned one and is treated as no.
    MVKConfiguration configuration{};
    std::size_t size = sizeof(configuration);
    return vkGetMoltenVKConfigurationMVK(VK_NULL_HANDLE, &configuration, &size) == VK_SUCCESS &&
           configuration.synchronousQueueSubmits == VK_TRUE;
}
void VulkanBackend::check_async_error() const {
    if (async_error_) std::rethrow_exception(async_error_);
}
void VulkanBackend::present(const Presenter& presenter, unsigned width, unsigned height) {
    present_async([&](VkDevice d, VkImage i, unsigned w, unsigned h) -> Completion {
        presenter(d, i, w, h);
        return {};
    }, width, height);
}
void VulkanBackend::present_async(const AsyncPresenter& presenter, unsigned width, unsigned height) {
    check_async_error();
    // Serialized: the previous presentation already retired at the reuse boundary.
    // QueueOrdered: retire the previous frame's presentation and render fence
    // here, one guest frame after they were committed, bounding run-ahead to one
    // presented frame without serializing CPU and GPU within a frame.
    drain();
    if (!image_valid_) throw std::runtime_error("Missing Vulkan image for Metal presentation");
    wait_stages_.resize(waits_.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = static_cast<std::uint32_t>(pending_commands_.size());
    submit.pCommandBuffers = pending_commands_.data();
    if (pending_commands_.empty()) {
        submit.waitSemaphoreCount = static_cast<std::uint32_t>(waits_.size());
        submit.pWaitSemaphores = waits_.data(); submit.pWaitDstStageMask = wait_stages_.data();
    }
    {
        const std::lock_guard lock(queue_mutex_);
        check(vkQueueSubmit(context_.queue, 1, &submit, fence_), "submit Metal handoff");
    }
    fence_pending_ = true;
    // The core's binary render-ready semaphore is consumed by this submission in
    // both modes; only the CPU wait for its completion moves in QueueOrdered.
    if (sync_ == HandoffSync::Serialized) settle_render_fence();
    waits_.clear(); pending_commands_.clear();
    const auto presenter_start = std::chrono::steady_clock::now();
    pending_completion_ = presenter(context_.device, image_.create_info.image, width, height);
    stats_.presenter_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - presenter_start).count();
    ++stats_.frames;
    if (signal_) {
        // Other cores may request an explicit image-release semaphore. Keep
        // that contract synchronous until a GPU-side release bridge exists.
        drain();
        fence_pending_ = true;
        submit = {}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &signal_;
        {
            const std::lock_guard lock(queue_mutex_);
            check(vkQueueSubmit(context_.queue, 1, &submit, fence_), "release Metal image");
        }
        settle_render_fence();
        signal_ = {};
    }
}
void VulkanBackend::ensure_buffer(VkDeviceSize size) {
    if (capacity_ >= size) return;
    if (buffer_) { vkDestroyBuffer(context_.device, buffer_, nullptr); buffer_ = {}; }
    if (memory_) { vkFreeMemory(context_.device, memory_, nullptr); memory_ = {}; }
    capacity_ = 0;
    VkBufferCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size; info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(context_.device, &info, nullptr, &buffer_), "create readback buffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context_.device, buffer_, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(context_.gpu, &properties);
    std::uint32_t type = properties.memoryTypeCount;
    const auto wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & wanted) == wanted) { type = i; break; }
    if (type == properties.memoryTypeCount) throw std::runtime_error("No coherent readback memory");
    VkMemoryAllocateInfo alloc{}; alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = requirements.size; alloc.memoryTypeIndex = type;
    check(vkAllocateMemory(context_.device, &alloc, nullptr, &memory_), "allocate readback memory");
    check(vkBindBufferMemory(context_.device, buffer_, memory_, 0), "bind readback memory");
    capacity_ = size;
}
const std::vector<std::uint8_t>& VulkanBackend::readback(unsigned width, unsigned height) {
    check_async_error();
    drain();
    if (!image_valid_ || !width || !height || width > 8192 || height > 8192)
        throw std::runtime_error("Invalid Vulkan output image");
    if (image_.create_info.format != VK_FORMAT_R8G8B8A8_UNORM)
        throw std::runtime_error("Unsupported Vulkan output format");
    const auto size = static_cast<VkDeviceSize>(width) * height * 4;
    ensure_buffer(size);
    check(vkResetCommandBuffer(command_, 0), "reset readback command");
    VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command_, &begin), "begin readback command");
    VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = image_.image_layout;
    barrier.newLayout = image_.image_layout == VK_IMAGE_LAYOUT_GENERAL ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_.create_info.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{}; region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(command_, barrier.image, barrier.newLayout, buffer_, 1, &region);
    std::swap(barrier.oldLayout, barrier.newLayout);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    check(vkEndCommandBuffer(command_), "end readback command");
    auto commands = pending_commands_; commands.push_back(command_);
    wait_stages_.resize(waits_.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = static_cast<std::uint32_t>(commands.size()); submit.pCommandBuffers = commands.data();
    if (pending_commands_.empty()) {
        submit.waitSemaphoreCount = static_cast<std::uint32_t>(waits_.size());
        submit.pWaitSemaphores = waits_.data(); submit.pWaitDstStageMask = wait_stages_.data();
    }
    submit.signalSemaphoreCount = signal_ ? 1 : 0; submit.pSignalSemaphores = &signal_;
    {
        const std::lock_guard lock(queue_mutex_);
        check(vkQueueSubmit(context_.queue, 1, &submit, fence_), "submit readback");
    }
    fence_pending_ = true;
    settle_render_fence();
    waits_.clear(); pending_commands_.clear(); signal_ = {};
    void* mapped = nullptr;
    check(vkMapMemory(context_.device, memory_, 0, size, 0, &mapped), "map readback");
    pixels_.resize(size);
    std::memcpy(pixels_.data(), mapped, size);
    vkUnmapMemory(context_.device, memory_);
    // Normalize RGBA to little-endian XRGB8888 (B,G,R,X) for all consumers.
    for (std::size_t i = 0; i < pixels_.size(); i += 4) std::swap(pixels_[i], pixels_[i + 2]);
    return pixels_;
}
}
