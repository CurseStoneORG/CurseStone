// Permission — a single permission node (e.g. "atomikpe.command.stop").
// Permissions form a tree with wildcards ("atomikpe.command.*").
// Mirrors pocketmine/permission/Permission.php.
#pragma once
#include <string>
namespace atomikpe::permission {
class Permission {
public:
    explicit Permission(std::string name, std::string description = "")
        : name_(std::move(name)), description_(std::move(description)) {}
    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    // Default value if no Permissible has set one (true=op-only, false=default-allow).
    bool defaultValue() const { return false; }
private:
    std::string name_;
    std::string description_;
};
} // namespace
