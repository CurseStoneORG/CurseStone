#pragma once
#include "Monster.h"
#include <string>
namespace cursestone::entity {
class EntitySkeleton : public Monster {
public:
    using Monster::Monster;
    EntitySkeleton(int64_t rid, std::shared_ptr<Level> l) : Monster(rid, std::move(l)) { setNetworkId(34); }
    std::string name() const { return "Skeleton"; }
};
} // namespace
