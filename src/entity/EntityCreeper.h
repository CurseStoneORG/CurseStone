#pragma once
#include "Monster.h"
#include <string>
namespace cenisyspe::entity {
class EntityCreeper : public Monster {
public:
    using Monster::Monster;
    EntityCreeper(int64_t rid, std::shared_ptr<Level> l) : Monster(rid, std::move(l)) { setNetworkId(33); }
    std::string name() const { return "Creeper"; }
};
} // namespace
