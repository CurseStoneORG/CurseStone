// CurseStone | vgen — mob herd helper stubs (dead code: the server
// does its own spawning; the biome registry only needs the symbols).
#pragma once
#include "vgen/biome/MobSpawnerData.h"

namespace vgen {

class MobSpawnUtils {
public:
	static void spawnHorseHerd(std::vector<Mob*>& mobHerd, Random& random) {
		UNUSED_VARIABLE(mobHerd);
		UNUSED_VARIABLE(random);
	}
	static void spawnPolarBearHerd(std::vector<Mob*>& mobHerd, Random& random) {
		UNUSED_VARIABLE(mobHerd);
		UNUSED_VARIABLE(random);
	}
	static EntityDefinitionIdentifier spawnZombie(Random& random) {
		UNUSED_VARIABLE(random);
		return EntityDefinitionIdentifier("zombie");
	}
};

} // namespace vgen

