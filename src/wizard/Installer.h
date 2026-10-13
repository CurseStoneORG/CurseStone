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
// Installer — first-run config seeding. As of the "no-setup" change,
// the server NEVER asks questions on first boot: CurseStone.cpp only calls
// shouldRun() + generateDefaultConfig(), which silently writes the
// default cursestone.yml (same content as resources/cursestone.yml) and
// logs one informational line. The interactive runWizard() is kept for
// manual/opt-in use (e.g. a future `--setup` flag) but is no longer on
// any startup path. Mirrors pocketmine/wizard/Installer.php minus the
// interactive flow.
#pragma once
#include <string>
namespace cursestone::wizard {
class Installer {
public:
    explicit Installer(std::string data_path) : data_path_(std::move(data_path)) {}
    bool runWizard();
    bool shouldRun() const;
    bool generateDefaultConfig();
private:
    std::string ask(const std::string& question, const std::string& def = "");
    bool askYesNo(const std::string& question, bool def = false);
    int askInt(const std::string& question, int def);
    std::string data_path_;
};
} // namespace
