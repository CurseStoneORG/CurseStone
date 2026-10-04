#pragma once
#include "Animal.h"
#include <string>
namespace cursestone::entity {
class EntityCow : public Animal {
public:
    using Animal::Animal;
    EntityCow(int64_t rid, std::shared_ptr<World> l) : Animal(rid, std::move(l)) { setNetworkId(11); }
    std::string name() const { return "Cow"; }
};
} // namespace
