#pragma once
#include "Animal.h"
namespace mcppeserver::entity {
class EntityWolf : public Animal {
public:
    using Animal::Animal;
    EntityWolf(int64_t rid, std::shared_ptr<Level> l) : Animal(rid, std::move(l)) { setNetworkId(14); }
    std::string name() const { return "Wolf"; }
};
} // namespace
