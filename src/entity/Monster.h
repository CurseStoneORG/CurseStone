// Monster — base for hostile mobs. Data-only (no AI pathing). The
// Server scheduler may move mobs around on tick to give a rough
// "wandering" feel, but proper mob AI is out of scope.
#pragma once
#include "Creature.h"
namespace cursestone::entity {
class Monster : public Creature {
public:
    using Creature::Creature;
};
} // namespace
