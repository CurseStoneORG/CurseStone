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
// TaskHandler — wraps a Task with scheduling metadata. The
// ServerScheduler owns a list of TaskHandlers and ticks them in
// order of nextRun.
#pragma once
#include "Task.h"
#include <memory>
namespace cursestone::scheduler {
class TaskHandler {
public:
    TaskHandler(uint64_t task_id, std::shared_ptr<Task> task)
        : task_id_(task_id), task_(std::move(task)) {}
    uint64_t taskId() const { return task_id_; }
    Task* task() const { return task_.get(); }
    // Called when the tick >= nextRun. Returns true if the task
    // should be removed (one-shot or cancelled).
    bool tick(uint64_t current_tick);
private:
    uint64_t task_id_;
    std::shared_ptr<Task> task_;
};
} // namespace
