// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
namespace ctr {
struct ScreenRect { double x, y, width, height; };
struct DualScreenLayout { ScreenRect top, bottom; };
struct TouchPoint { std::int16_t x, y; };
inline bool valid_slot(ScreenRect r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
           r.x >= 0 && r.y >= 0 && r.width > 0 && r.height > 0 &&
           r.x + r.width <= 1 && r.y + r.height <= 1;
}
inline ScreenRect fit_screen(ScreenRect slot, double width, double height, double aspect) {
    const double availableWidth=slot.width*width, availableHeight=slot.height*height;
    const double fittedWidth=std::min(availableWidth,availableHeight*aspect);
    const double fittedHeight=fittedWidth/aspect;
    return {slot.x*width+(availableWidth-fittedWidth)/2,
            slot.y*height+(availableHeight-fittedHeight)/2,fittedWidth,fittedHeight};
}
// Input slots are normalized to the whole output surface. Returned rectangles
// are in the coordinate units of width/height (pixels for rendering, points for UI).
inline DualScreenLayout fit_dual_screens(DualScreenLayout slots,double width,double height) {
    if(!valid_slot(slots.top) || !valid_slot(slots.bottom) || !std::isfinite(width) ||
       !std::isfinite(height) || width<=0 || height<=0)
        throw std::invalid_argument("Invalid dual-screen layout");
    return {fit_screen(slots.top,width,height,400.0/240),fit_screen(slots.bottom,width,height,320.0/240)};
}
// Map only the visible bottom screen, excluding its aspect-fit letterbox.
// Coordinates are libretro POINTER coordinates in the default 400x480 frame.
inline std::optional<TouchPoint> map_bottom_touch(const DualScreenLayout& fitted,double x,double y) {
    const auto r=fitted.bottom;
    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(r.x) || !std::isfinite(r.y) ||
       !std::isfinite(r.width) || !std::isfinite(r.height) ||
       !std::isfinite(r.x+r.width) || !std::isfinite(r.y+r.height) || r.width<=0 || r.height<=0 ||
       x<r.x || x>r.x+r.width || y<r.y || y>r.y+r.height) return std::nullopt;
    const double u=(x-r.x)/r.width, v=(y-r.y)/r.height;
    return TouchPoint{static_cast<std::int16_t>(std::lround(((40+u*320)/400)*65534-32767)),
                      static_cast<std::int16_t>(std::lround(((240+v*240)/480)*65534-32767))};
}
}
