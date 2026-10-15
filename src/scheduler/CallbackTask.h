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
// CallbackTask — wraps a std::function<void()> as a Task. Used
// by the Server to defer one-shot work onto the main thread.
#pragma once
#include "Task.h"
#include <functional>
namespace cursestone::scheduler {
class CallbackTask : public Task {
  public:
    explicit CallbackTask(std::function<void()> fn) : fn_(std::move(fn)) {
    }
    void onRun() override {
        if (fn_)
            fn_();
    }

  private:
    std::function<void()> fn_;
};
} // namespace
