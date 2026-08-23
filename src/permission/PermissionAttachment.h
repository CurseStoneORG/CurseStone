#pragma once
#include <string>
#include <unordered_map>
namespace cenisyspe::permission {
class PermissionAttachment {
public:
    void setPermission(const std::string& name, bool value) { perms_[name] = value; }
    const std::unordered_map<std::string, bool>& permissions() const { return perms_; }
private:
    std::unordered_map<std::string, bool> perms_;
};
} // namespace
