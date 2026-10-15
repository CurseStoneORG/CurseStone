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
// SPDX-License-Identifier: LGPL-3.0-or-later
// CompressBatchedTask.cpp — zlib compress on the async pool.
#include "CompressBatchedTask.h"

#include <zlib.h>

namespace cursestone {
namespace network {

void CompressBatchedTask::onRun() {
    uLong srcLen = static_cast<uLong>(payload_.size());
    uLong bound = compressBound(srcLen);
    std::vector<uint8_t> tmp(bound);
    uLongf dstLen = bound;
    int r = compress2(tmp.data(), &dstLen, payload_.data(), srcLen,
                      level_ >= 1 && level_ <= 9 ? level_ : 7);
    if (r != Z_OK) {
        result_.clear();
        return;
    }
    result_.assign(tmp.data(), tmp.data() + dstLen);
}

void CompressBatchedTask::onCompletion() {
    if (cb_)
        cb_(std::move(result_));
}

} // namespace network
} // namespace cursestone
