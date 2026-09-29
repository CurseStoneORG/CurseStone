#pragma once
#include "Attribute.h"
#include <string>
#include <unordered_map>
namespace cursestone::entity {
class AttributeMap {
public:
    void set(const Attribute& a) { map_[a.name()] = a; }
    Attribute get(const std::string& name) const {
        auto it = map_.find(name);
        return it == map_.end() ? Attribute(name, 0, 0, 0) : it->second;
    }
    const std::unordered_map<std::string, Attribute>& all() const { return map_; }
private:
    std::unordered_map<std::string, Attribute> map_;
};
} // namespace
