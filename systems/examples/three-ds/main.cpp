// SPDX-License-Identifier: GPL-3.0-or-later
#include <runtime.hpp>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("Usage: independent-three-ds original-probe.3dsx isolated-storage");
        ctr::Runtime runtime(argv[2]);runtime.load(argv[1]);
        for(unsigned i=0;i<180;++i)runtime.step();
        if(!runtime.probe_seen())throw std::runtime_error("Original ARM11 probe marker missing");
        runtime.unload();std::cout<<"Standalone 3DS consumer passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
