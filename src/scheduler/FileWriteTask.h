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
// FileWriteTask — AsyncTask that writes a buffer to disk. Used by
// ChunkManager to flush chunks to anvil region files.
#pragma once
#include "Task.h"
#include <string>
#include <vector>
#include <cstdint>
namespace cursestone::scheduler {
class FileWriteTask : public AsyncTask {
public:
    FileWriteTask(std::string path, std::vector<uint8_t> bytes)
        : path_(std::move(path)), bytes_(std::move(bytes)) {}
    void onRun() override;
private:
    std::string path_;
    std::vector<uint8_t> bytes_;
};
} // namespace
