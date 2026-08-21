// Nameable — tile that has a custom name (chest with custom name).
#pragma once
#include <string>
namespace mcppeserver::tile {
class Nameable {
public:
    virtual ~Nameable() = default;
    virtual const std::string& getName() const { return name_; }
    virtual void setName(const std::string& s) { name_ = s; }
private:
    std::string name_;
};
} // namespace
