// InstallerLang — localized strings for the first-run wizard.
// Loaded from resources/eng.ini (English locale). Mirrors
// pocketmine/wizard/InstallerLang.php.
#pragma once
#include <string>
#include <unordered_map>
namespace mcppeserver::wizard {
class InstallerLang {
public:
    static InstallerLang& get();
    bool load(const std::string& lang_code);
    const std::string& get(const std::string& key) const;
private:
    std::unordered_map<std::string, std::string> strings_;
    std::string missing_ = "[?]";
};
} // namespace
