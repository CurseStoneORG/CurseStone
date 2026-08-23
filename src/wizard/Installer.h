// Installer — first-run setup wizard. Asks the user for:
//   - language
//   - server name (motd)
//   - max players
//   - port
//   - level name + type (normal/flat)
//   - gamemode
//   - difficulty
//   - whether to enable whitelist
//
// Saves the result as cenisyspe.yml. Mirrors pocketmine/wizard
// /Installer.php.
#pragma once
#include <string>
namespace cenisyspe::wizard {
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
