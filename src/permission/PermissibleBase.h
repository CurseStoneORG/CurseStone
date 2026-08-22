// PermissibleBase — default Permissible implementation. Holds a
// map of permission nodes to boolean values. Wildcards are
// expanded lazily on lookup.
#pragma once
#include "Permissible.h"
#include <string>
#include "PermissionAttachment.h"
#include <unordered_map>
#include <memory>
#include <vector>
namespace mcppeserver::permission {
class PermissibleBase : public Permissible {
public:
    bool hasPermission(const std::string& name) const override;
    void recalculatePermissions() override;
    void setPermission(const std::string& name, bool value);
    void removePermission(const std::string& name);
    PermissionAttachment* attach(std::shared_ptr<PermissionAttachment> a) {
        attachments_.push_back(a);
        recalculatePermissions();
        return a.get();
    }
private:
    std::unordered_map<std::string, bool> perms_;
    std::vector<std::shared_ptr<PermissionAttachment>> attachments_;
};
} // namespace
