// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc < 3) throw std::runtime_error("Usage: ctr-prepare data-directory base-game [update.cia ...]");
        ctr::Runtime runtime(argv[1]);
        const auto base = runtime.inspect(argv[2]);
        std::cout << "Base metadata: id=" << std::hex << base.title_id << std::dec
                  << " version=" << base.version << " version_known=" << unsigned(base.version_known)
                  << " cia_encrypted=" << unsigned(base.cia_content_encrypted) << '\n';
        if ((base.title_id >> 32) != 0x00040000)
            throw std::runtime_error("Select the base application, not an update or system title");
        // Validate every update against the base before installing any package.
        for (int i = 3; i < argc; ++i) {
            const auto update = runtime.inspect(argv[i]);
            std::cout << "Update metadata: id=" << std::hex << update.title_id << std::dec
                      << " version=" << update.version
                      << " cia_encrypted=" << unsigned(update.cia_content_encrypted) << '\n';
            if (!update.is_cia || (update.title_id >> 32) != 0x0004000e ||
                (update.title_id & 0xffffffff) != (base.title_id & 0xffffffff))
                throw std::runtime_error("Update does not match this base title: " + std::string(argv[i]));
        }
        if (base.cia_content_encrypted)
            throw std::runtime_error("Base CIA content is encrypted; provide a decrypted export");
        for (int i = 3; i < argc; ++i) {
            if (runtime.inspect(argv[i]).cia_content_encrypted)
                throw std::runtime_error("Update CIA content is encrypted; provide a decrypted export: " + std::string(argv[i]));
        }
        auto launch = std::filesystem::absolute(argv[2]);
        if (base.is_cia) launch = runtime.install_package(argv[2]);
        for (int i = 3; i < argc; ++i) {
            const auto update = runtime.inspect(argv[i]);
            runtime.install_package(argv[i]);
            std::cout << "Installed update " << std::hex << std::setw(16) << std::setfill('0')
                      << update.title_id << std::dec << " version " << update.version << '\n';
        }
        std::cout << "Base title " << std::hex << std::setw(16) << std::setfill('0') << base.title_id
                  << std::dec << "\nLaunch path: " << launch.string() << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
