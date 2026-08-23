// SPDX-License-Identifier: LGPL-3.0-or-later
// CenisysPE entry point.
// Initializes the server singleton and runs the main tick loop.
#include "main.h"
#include "Server.h"
#include "wizard/Installer.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>

namespace {
    std::atomic<bool> g_shouldRun{true};

    void handleSignal(int sig) {
        (void)sig;
        g_shouldRun.store(false, std::memory_order_relaxed);
        // Also tell the server to stop its tick loop.
        cenisyspe::Server::getInstance().shutdown();
    }
}

int main(int argc, char* argv[]) {
    using namespace cenisyspe;

    // Banner
    std::cout << CENISYSPE_NAME << " v" << CENISYSPE_VERSION
              << " \"" << CENISYSPE_CODENAME << "\"\n"
              << "Targeting Minecraft " << MINECRAFT_VERSION
              << " (protocol " << PROTOCOL_VERSION << ")\n\n";

    // Install signal handlers for clean shutdown
    std::signal(SIGINT,  handleSignal);
    std::signal(SIGTERM, handleSignal);

    // Resolve data path (default: current working directory)
    std::string dataPath = ".";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            dataPath = argv[++i];
        }
    }

    // First-run setup wizard
    wizard::Installer installer(dataPath);
    if (installer.shouldRun()) {
        if (!installer.runWizard()) {
            std::cerr << "Setup failed; generating default config instead.\n";
            installer.generateDefaultConfig();
        }
    }

    // Boot the server singleton
    auto& server = Server::getInstance();
    server.start(dataPath);
    server.runTickLoop();
    return 0;
}
