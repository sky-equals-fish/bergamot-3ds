// SPDX-License-Identifier: GPL-3.0-or-later
#include "metal_presenter.h"
#include "display_layout.hpp"
#include <mutex>
#include <vulkan/vulkan_metal.h>
#include <algorithm>
#include <stdexcept>
#include <cstdio>

id<MTLTexture> CTRExportTexture(VkDevice device, VkImage image) {
    VkExportMetalTextureInfoEXT texture{};
    texture.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT;
    texture.image = image; texture.plane = VK_IMAGE_ASPECT_COLOR_BIT;
    VkExportMetalObjectsInfoEXT objects{};
    objects.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT;
    objects.pNext = &texture;
    auto exportObjects = reinterpret_cast<PFN_vkExportMetalObjectsEXT>(vkGetDeviceProcAddr(device, "vkExportMetalObjectsEXT"));
    if (!exportObjects) throw std::runtime_error("Metal texture export is unavailable");
    exportObjects(device, &objects);
    if (!texture.mtlTexture) throw std::runtime_error("Metal texture export returned no image");
    return texture.mtlTexture;
}
id<MTLCommandQueue> CTRExportCommandQueue(VkDevice device, VkQueue queue) {
    VkExportMetalCommandQueueInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_COMMAND_QUEUE_INFO_EXT;
    info.queue = queue;
    VkExportMetalObjectsInfoEXT objects{};
    objects.sType = VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT;
    objects.pNext = &info;
    auto exportObjects = reinterpret_cast<PFN_vkExportMetalObjectsEXT>(vkGetDeviceProcAddr(device, "vkExportMetalObjectsEXT"));
    if (!exportObjects || !queue) throw std::runtime_error("Metal command queue export is unavailable");
    exportObjects(device, &objects);
    if (!info.mtlCommandQueue) throw std::runtime_error("Metal command queue export returned no queue");
    return info.mtlCommandQueue;
}

@interface CTRMetalPresenter ()
- (instancetype)initWithDevice:(id<MTLDevice>)device queue:(id<MTLCommandQueue>)queue;
@end

@implementation CTRMetalPresenter {
    id<MTLCommandQueue> _queue;
    id<MTLRenderPipelineState> _pipeline;
    id<MTLDevice> _device;
    double _acquireSeconds, _drawSeconds;
    unsigned _samples;
    std::mutex _layoutMutex;
    std::optional<ctr::DualScreenLayout> _screenSlots;
}
- (BOOL)setTopScreenSlot:(CGRect)top bottomScreenSlot:(CGRect)bottom {
    ctr::DualScreenLayout slots{{top.origin.x, top.origin.y, top.size.width, top.size.height},
        {bottom.origin.x, bottom.origin.y, bottom.size.width, bottom.size.height}};
    if (!ctr::valid_slot(slots.top) || !ctr::valid_slot(slots.bottom)) return NO;
    const std::lock_guard lock(_layoutMutex);
    _screenSlots = slots;
    return YES;
}
- (void)clearScreenSlots {
    const std::lock_guard lock(_layoutMutex);
    _screenSlots.reset();
}
- (instancetype)initWithDevice:(id<MTLDevice>)device {
    return [self initWithDevice:device queue:[device newCommandQueue]];
}
- (instancetype)initWithCommandQueue:(id<MTLCommandQueue>)queue {
    if (!queue) throw std::runtime_error("Missing Metal command queue");
    return [self initWithDevice:queue.device queue:queue];
}
- (instancetype)initWithDevice:(id<MTLDevice>)device queue:(id<MTLCommandQueue>)queue {
    if ((self = [super init])) {
        _device = device;
        _queue = queue;
        NSString* source = @"#include <metal_stdlib>\nusing namespace metal;\n"
            "struct V { float4 p [[position]]; float2 uv; };\n"
            "vertex V vertexMain(uint i [[vertex_id]]) {\n"
            "float2 p[3] = {float2(-1,-1),float2(3,-1),float2(-1,3)};\n"
            "return {float4(p[i],0,1), float2((p[i].x+1)*0.5,(1-p[i].y)*0.5)}; }\n"
            "fragment float4 fragmentMain(V v [[stage_in]], texture2d<float> t [[texture(0)]], constant float4& crop [[buffer(0)]]) {\n"
            "constexpr sampler s(filter::linear, address::clamp_to_edge); float2 halfTexel = 0.5 / float2(t.get_width(),t.get_height()); float2 uv = clamp(crop.xy + v.uv * crop.zw, crop.xy + halfTexel, crop.xy + crop.zw - halfTexel); return float4(t.sample(s,uv).rgb,1); }\n";
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library) throw std::runtime_error(error.localizedDescription.UTF8String);
        MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        descriptor.vertexFunction = [library newFunctionWithName:@"vertexMain"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"fragmentMain"];
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        _pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!_pipeline || !_queue) throw std::runtime_error("Could not create Metal presenter");
    }
    return self;
}
- (void)draw:(id<MTLTexture>)source to:(id<MTLTexture>)target drawable:(id<CAMetalDrawable>)drawable {
    CTRWaitForPresentation([self enqueueDraw:source to:target drawable:drawable]);
}
- (id<MTLCommandBuffer>)enqueueDraw:(id<MTLTexture>)source to:(id<MTLTexture>)target drawable:(id<CAMetalDrawable>)drawable {
    std::optional<ctr::DualScreenLayout> slots;
    { const std::lock_guard lock(_layoutMutex); slots = _screenSlots; }
    if (source.device != _device || target.device != _device)
        throw std::runtime_error("Presentation textures belong to different Metal devices");
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, (slots || self.manicPortrait) ? 0 : 1);
    id<MTLCommandBuffer> command = [_queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!command || !encoder) throw std::runtime_error("Could not encode Metal presentation");
    const double scale = std::min(static_cast<double>(target.width) / source.width, static_cast<double>(target.height) / source.height);
    const double width = source.width * scale, height = source.height * scale;
    [encoder setViewport:MTLViewport{(target.width - width) / 2, (target.height - height) / 2, width, height, 0, 1}];
    [encoder setRenderPipelineState:_pipeline];
    [encoder setFragmentTexture:source atIndex:0];
    if (slots) {
        const auto layout = ctr::fit_dual_screens(*slots, target.width, target.height);
        const ctr::ScreenRect frames[2] = {layout.top, layout.bottom};
        const float crops[2][4] = {{0, 0, 1, 0.5}, {0.1, 0.5, 0.8, 0.5}};
        for (unsigned i = 0; i < 2; ++i) {
            const auto f = frames[i];
            const NSUInteger left = std::min(target.width, static_cast<NSUInteger>(std::ceil(f.x)));
            const NSUInteger top = std::min(target.height, static_cast<NSUInteger>(std::ceil(f.y)));
            const NSUInteger right = std::min(target.width, static_cast<NSUInteger>(std::floor(f.x + f.width)));
            const NSUInteger bottom = std::min(target.height, static_cast<NSUInteger>(std::floor(f.y + f.height)));
            if (right <= left || bottom <= top) continue;
            [encoder setScissorRect:MTLScissorRect{left, top, right-left, bottom-top}];
            [encoder setViewport:MTLViewport{f.x, f.y, f.width, f.height, 0, 1}];
            [encoder setFragmentBytes:crops[i] length:sizeof(crops[i]) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
    } else if (self.manicPortrait) {
        // Azahar default layout is 400x480: bottom screen is 320x240,
        // centered at x=40, y=240. Maniac uses a different source convention.
        const float crops[2][4] = {{0, 0, 1, 0.5}, {0.1, 0.5, 0.8, 0.5}};
        const double frames[2][4] = {{8, 55, 359, 215}, {40, 273, 295, 221}};
        for (unsigned i = 0; i < 2; ++i) {
            const auto& f = frames[i];
            // Viewports transform vertices; they do not clip an oversized
            // fullscreen triangle to a sub-screen. Clip each draw explicitly.
            const NSUInteger left = f[0] / 375 * target.width;
            const NSUInteger top = f[1] / 812 * target.height;
            const NSUInteger right = (f[0] + f[2]) / 375 * target.width;
            const NSUInteger bottom = (f[1] + f[3]) / 812 * target.height;
            [encoder setScissorRect:MTLScissorRect{left, top, right-left, bottom-top}];
            [encoder setViewport:MTLViewport{f[0] / 375 * target.width, f[1] / 812 * target.height,
                f[2] / 375 * target.width, f[3] / 812 * target.height, 0, 1}];
            [encoder setFragmentBytes:crops[i] length:sizeof(crops[i]) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
    } else {
        const float crop[4] = {0, 0, 1, 1};
        [encoder setFragmentBytes:crop length:sizeof(crop) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }
    [encoder endEncoding];
    if (drawable) [command presentDrawable:drawable];
    [command commit];
    return command;
}
- (id<MTLCommandBuffer>)enqueuePresent:(id<MTLTexture>)source on:(CAMetalLayer*)layer {
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (!drawable) return nil;
        return [self enqueueDraw:source to:drawable.texture drawable:drawable];
    }
}
- (void)present:(id<MTLTexture>)source on:(CAMetalLayer*)layer {
    @autoreleasepool {
        const double start = CACurrentMediaTime();
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        const double acquired = CACurrentMediaTime();
        if (!drawable) return; // Background/occluded surfaces may have no drawable.
        [self draw:source to:drawable.texture drawable:drawable];
        _acquireSeconds += acquired - start;
        _drawSeconds += CACurrentMediaTime() - acquired;
        if (++_samples == 120) {
            std::fprintf(stderr, "CTR_PRESENT acquire_ms=%.3f draw_wait_ms=%.3f\n", _acquireSeconds * 1000 / _samples, _drawSeconds * 1000 / _samples);
            _acquireSeconds = _drawSeconds = 0;
            _samples = 0;
        }
    }
}
@end

void CTRWaitForPresentation(id<MTLCommandBuffer> command) {
    if (!command) return;
    [command waitUntilCompleted];
    if (command.status == MTLCommandBufferStatusError) {
        const char* message = command.error.localizedDescription.UTF8String;
        throw std::runtime_error(message ? message : "Metal presentation failed");
    }
}
