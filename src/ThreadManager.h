/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
/**
 * @brief Registry of server threads that are joined at shutdown.
 *
 * Stores shared thread handles and joins every joinable one in
 * joinAll(), clearing the registry afterwards.
 */
#pragma once
#include <thread>
#include <vector>
#include <memory>
namespace cursestone {
/**
 * @brief Holds the thread handles the server must join before exiting.
 */
class ThreadManager {
public:
    /// Returns the process-wide instance.
    static ThreadManager& get();

    /// Hands a thread handle to the registry so joinAll() can join it later.
    void registerThread(std::shared_ptr<std::thread> t) { threads_.push_back(std::move(t)); }

    /// Joins every registered, joinable thread and clears the registry.
    void joinAll();
private:
    std::vector<std::shared_ptr<std::thread>> threads_;
};
} // namespace
