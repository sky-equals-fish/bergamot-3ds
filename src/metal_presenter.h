// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <vulkan/vulkan.h>

id<MTLTexture> CTRExportTexture(VkDevice device, VkImage image);
// MoltenVK's MTLCommandQueue behind the core's VkQueue (VK_EXT_metal_objects).
// A presenter created from it shares the core's in-order Metal queue, which is
// what ctr::VulkanBackend::HandoffSync::QueueOrdered relies on.
id<MTLCommandQueue> CTRExportCommandQueue(VkDevice device, VkQueue queue);

@interface CTRMetalPresenter : NSObject
@property(nonatomic) BOOL manicPortrait;
// Normalized output slots, top-left origin; safe to update from the UI thread.
// Both screens preserve their original aspect ratios. Invalid layouts return NO.
- (BOOL)setTopScreenSlot:(CGRect)top bottomScreenSlot:(CGRect)bottom;
- (void)clearScreenSlots;
- (instancetype)initWithDevice:(id<MTLDevice>)device;
// Commits presentation on an existing queue instead of a private one.
- (instancetype)initWithCommandQueue:(id<MTLCommandQueue>)queue;
// Synchronous: the source image can be reused after this method returns.
- (void)draw:(id<MTLTexture>)source to:(id<MTLTexture>)target drawable:(id<CAMetalDrawable>)drawable;
- (void)present:(id<MTLTexture>)source on:(CAMetalLayer*)layer;
// Opt-in deferred path: caller must wait on the returned command before the
// producer overwrites the source or destroys its Vulkan resources.
- (id<MTLCommandBuffer>)enqueueDraw:(id<MTLTexture>)source to:(id<MTLTexture>)target drawable:(id<CAMetalDrawable>)drawable;
- (id<MTLCommandBuffer>)enqueuePresent:(id<MTLTexture>)source on:(CAMetalLayer*)layer;
@end

void CTRWaitForPresentation(id<MTLCommandBuffer> command);
