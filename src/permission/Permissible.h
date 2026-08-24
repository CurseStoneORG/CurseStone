// Permissible — interface for objects that can hold permissions
// (Players, CommandSenders).
#pragma once
#include <string>
namespace atomikpe::permission {
class Permissible {
public:
    virtual ~Permissible() = default;
    virtual bool hasPermission(const std::string& name) const = 0;
    virtual void recalculatePermissions() = 0;
};
} // namespace
