#include "InstallerLang.h"
#include "utils/Config.h"
#include <fstream>
#include <sstream>
namespace cenisyspe::wizard {
InstallerLang& InstallerLang::get() { static InstallerLang i; return i; }
bool InstallerLang::load(const std::string& lang_code) {
    utils::Config cfg;
    std::string path = "resources/" + lang_code + ".ini";
    if (!cfg.loadIni(path)) return false;
    // Populate strings_ from cfg — Config doesn't expose a direct
    // iterator, so we re-load and parse here.
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        strings_[k] = v;
    }
    return !strings_.empty();
}
const std::string& InstallerLang::get(const std::string& key) const {
    auto it = strings_.find(key);
    return it == strings_.end() ? missing_ : it->second;
}
} // namespace
