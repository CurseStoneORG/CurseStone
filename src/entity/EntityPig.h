#pragma once
#include "Animal.h"
#include <string>
namespace atomikpe::entity {
class EntityPig : public Animal {
public:
    using Animal::Animal;
    EntityPig(int64_t rid, std::shared_ptr<Level> l) : Animal(rid, std::move(l)) { setNetworkId(12); }
    std::string name() const { return "Pig"; }
};
} // namespace
