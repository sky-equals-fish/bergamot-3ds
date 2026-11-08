// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_layout.hpp"
#include <iostream>
#include <limits>
void check(bool okay) { if(!okay) throw std::runtime_error("Display layout contract failed"); }
int main() {
    const ctr::DualScreenLayout stacked{{0,0,1,0.5},{0,0.5,1,0.5}};
    auto fit=ctr::fit_dual_screens(stacked,400,480);
    check(fit.top.x==0 && fit.top.width==400 && fit.bottom.x==40 && fit.bottom.width==320);
    auto center=ctr::map_bottom_touch(fit,200,360);
    check(center && center->x==0 && center->y==16384);
    check(!ctr::map_bottom_touch(fit,20,360));
    check(!ctr::map_bottom_touch(fit,200,120));
    auto corner=ctr::map_bottom_touch(fit,360,480);
    check(corner && corner->x==26214 && corner->y==32767);
    // Wide layout and changed pixel density must retain aspect ratios and input identity.
    const ctr::DualScreenLayout sideBySide{{0,0,0.5,1},{0.5,0,0.5,1}};
    auto wide=ctr::fit_dual_screens(sideBySide,1200,800);
    check(std::abs(wide.top.width/wide.top.height-400.0/240)<1e-10);
    check(std::abs(wide.bottom.width/wide.bottom.height-320.0/240)<1e-10);
    auto wideCenter=ctr::map_bottom_touch(wide,900,400);
    check(wideCenter && wideCenter->x==center->x && wideCenter->y==center->y);
    check(!ctr::map_bottom_touch(wide,900,10));
    bool rejected=false;
    try { (void)ctr::fit_dual_screens(stacked,0,800); } catch(const std::invalid_argument&) {rejected=true;}
    check(rejected);
    check(!ctr::valid_slot({0,0,std::numeric_limits<double>::quiet_NaN(),1}));
    check(!ctr::valid_slot({0.9,0,0.2,1}));
    fit.bottom.width=std::numeric_limits<double>::quiet_NaN();
    check(!ctr::map_bottom_touch(fit,200,360));
    std::cout<<"display_layout_pass: portrait, wide, aspect ratios, touch bounds, invalid dimensions\n";
}
