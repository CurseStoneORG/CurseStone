#include "PermissibleBase.h"
namespace cursestone::permission {
bool PermissibleBase::hasPermission(const std::string& name) const {
    // Exact match
    if (auto it = perms_.find(name); it != perms_.end()) return it->second;
    // Wildcard walk up the tree.
    std::string cur = name;
    while (true) {
        auto dot = cur.rfind('.');
        if (dot == std::string::npos) break;
        cur = cur.substr(0, dot) + ".*";
        if (auto it = perms_.find(cur); it != perms_.end()) return it->second;
        cur.pop_back(); cur.pop_back();  // strip ".*"
    }
    return false;
}
void PermissibleBase::setPermission(const std::string& name, bool value) {
    perms_[name] = value;
}
void PermissibleBase::removePermission(const std::string& name) {
    perms_.erase(name);
}
void PermissibleBase::recalculatePermissions() {
    perms_.clear();
    for (auto& a : attachments_) {
        for (auto& kv : a->permissions()) perms_[kv.first] = kv.second;
    }
}
} // namespace
