#pragma once
#include "Animal.h"
#include <string>
namespace cursestone::entity {
class EntitySheep : public Animal {
public:
    using Animal::Animal;
    EntitySheep(int64_t rid, std::shared_ptr<Level> l) : Animal(rid, std::move(l)) { setNetworkId(13); }
    std::string name() const { return "Sheep"; }
};
} // namespace
