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
// CompressBatchedTask — background zlib compression of a batch of
// sub-packets. Run on the AsyncPool. C++.
#pragma once

#include "../scheduler/Task.h"
#include "protocol/BatchPacket.h"
#include <vector>
#include <functional>
#include <memory>

namespace cursestone::network {

class CompressBatchedTask : public scheduler::AsyncTask {
public:
    using CompletionCb = std::function<void(std::vector<uint8_t>)>;

    CompressBatchedTask(std::vector<uint8_t> payload, int level,
                          CompletionCb cb)
        : payload_(std::move(payload)), level_(level), cb_(std::move(cb)) {}

    void onRun() override;          // worker thread: do zlib compression
    void onCompletion() override;    // main thread: deliver result via cb_

private:
    std::vector<uint8_t> payload_;
    int                   level_;
    CompletionCb          cb_;
    std::vector<uint8_t>  result_;   // populated by onRun()
};

} // namespace cursestone::network
