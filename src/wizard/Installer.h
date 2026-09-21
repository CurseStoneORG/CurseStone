// Installer — first-run config seeding. As of the "no-setup" change,
// the server NEVER asks questions on first boot: main.cpp only calls
// shouldRun() + generateDefaultConfig(), which silently writes the
// default atomikpe.yml (same content as resources/atomikpe.yml) and
// logs one informational line. The interactive runWizard() is kept for
// manual/opt-in use (e.g. a future `--setup` flag) but is no longer on
// any startup path. Mirrors pocketmine/wizard/Installer.php minus the
// interactive flow.
#pragma once
#include <string>
namespace atomikpe::wizard {
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
