#pragma once
#include "Animal.h"
#include <string>
namespace cenisyspe::entity {
class EntityChicken : public Animal {
public:
    using Animal::Animal;
    EntityChicken(int64_t rid, std::shared_ptr<Level> l) : Animal(rid, std::move(l)) { setNetworkId(10); }
    std::string name() const { return "Chicken"; }
};
} // namespace
