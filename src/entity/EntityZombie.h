#pragma once
#include "Monster.h"
namespace mcppeserver::entity {
class EntityZombie : public Monster {
public:
    using Monster::Monster;
    EntityZombie(int64_t rid, std::shared_ptr<Level> l) : Monster(rid, std::move(l)) { setNetworkId(32); }
    std::string name() const { return "Zombie"; }
};
} // namespace
