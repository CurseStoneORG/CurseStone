// SPDX-License-Identifier: LGPL-3.0-or-later
// WorkbenchCommand.h — /workbench command for server diagnostics.
#pragma once
#include "command/Command.h"
#include <string>
namespace cursestone {
class WorkbenchCommand : public Command {
public:
    WorkbenchCommand() : Command("workbench", "Server diagnostics: start/stop/status/report") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
