// SPDX-License-Identifier: GPL-3.0-or-later
#include "metal_presenter.h"
#include <vector>
#include <iostream>
#include <stdexcept>
int main() { @autoreleasepool {
    try {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        auto* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:400 height:480 mipmapped:NO];
        desc.storageMode = MTLStorageModeShared;
        id<MTLTexture> source = [device newTextureWithDescriptor:desc];
        std::vector<unsigned char> pixels(400*480*4,0);
        for(unsigned y=0;y<480;++y) for(unsigned x=0;x<400;++x) {
            auto* p = &pixels[(y*400+x)*4]; p[3]=255;
            if(y<240) p[2]=255;
            else if(x>=40 && x<360) p[1]=255;
        }
        [source replaceRegion:MTLRegionMake2D(0,0,400,480) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:1600];
        desc.width=750; desc.height=1624; desc.usage=MTLTextureUsageRenderTarget;
        id<MTLTexture> target=[device newTextureWithDescriptor:desc];
        CTRMetalPresenter* presenter=[[CTRMetalPresenter alloc] initWithDevice:device];
        presenter.manicPortrait=YES;
        [presenter draw:source to:target drawable:nil];
        pixels.resize(750*1624*4);
        [target getBytes:pixels.data() bytesPerRow:3000 fromRegion:MTLRegionMake2D(0,0,750,1624) mipmapLevel:0];
        // Exact integer 2x layout checks every destination pixel, including
        // transparent gaps and areas outside both screen rectangles.
        for(unsigned y=0;y<1624;++y) for(unsigned x=0;x<750;++x) {
            const auto* p=&pixels[(y*750+x)*4];
            bool top=x>=16 && x<734 && y>=110 && y<540;
            bool bottom=x>=80 && x<670 && y>=546 && y<988;
            if (p[3] != (top||bottom ? 255:0) || (top && p[2]<254) || (bottom && p[1]<254))
                throw std::runtime_error("Skin screen crop/clip mismatch at "+std::to_string(x)+","+std::to_string(y));
        }
        // UI-defined wide layout: native aspect ratios in equal horizontal slots.
        if (![presenter setTopScreenSlot:CGRectMake(0,0,0.5,1) bottomScreenSlot:CGRectMake(0.5,0,0.5,1)])
            throw std::runtime_error("Valid custom layout rejected");
        if ([presenter setTopScreenSlot:CGRectMake(-1,0,1,1) bottomScreenSlot:CGRectMake(0,0,1,1)])
            throw std::runtime_error("Invalid custom layout accepted");
        desc.width=1200; desc.height=800;
        target=[device newTextureWithDescriptor:desc];
        [presenter draw:source to:target drawable:nil];
        pixels.resize(1200*800*4);
        [target getBytes:pixels.data() bytesPerRow:4800 fromRegion:MTLRegionMake2D(0,0,1200,800) mipmapLevel:0];
        for(unsigned y=0;y<800;++y) for(unsigned x=0;x<1200;++x) {
            const auto* p=&pixels[(y*1200+x)*4];
            bool top=x<600 && y>=220 && y<580;
            bool bottom=x>=600 && y>=175 && y<625;
            if(p[3]!=(top||bottom ? 255:0) || (top && p[2]<254) || (bottom && p[1]<254))
                throw std::runtime_error("Custom layout crop/clip mismatch");
        }
        [presenter clearScreenSlots];
        std::cout << "skin_presenter_pass: both screens and transparent border verified\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
} }
