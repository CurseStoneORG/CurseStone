// CurseStone | vgen — mob spawn list entry stub.
// The vanilla Biome registry carries per-biome mob lists; this server
// does its own spawning, so the entries are compile-only stubs that
// keep the ported Biome code building unchanged.
#pragma once
#include "vgen/Vanilla.h"
#include "vgen/WeighedRandom.h"
#include <functional>
#include <string>
#include <vector>

namespace vgen {

enum class EntityType : int {
	AnimalHostile = 0,
	Monster,
	WaterMob,
	Ambient,
	Creature,
	Player,
	Other,
	// named entries the ported biome/structure code references
	Animal,
	WaterAnimal,
	Bat,
	CaveSpider,
	Chest,
	Chicken,
	Cow,
	Creeper,
	EnderMan,
	FlowerPot,
	Horse,
	Husk,
	MinecartChest,
	MushroomCow,
	Ocelot,
	Pig,
	PolarBear,
	Rabbit,
	Sheep,
	Skeleton,
	Slime,
	Spider,
	Squid,
	Stray,
	Villager,
	Witch,
	Wolf,
	Zombie,
	ZombieVillager
};

struct EntityDefinitionIdentifier {
	std::string mNamespace = "minecraft";
	std::string mName;
	std::string mFilter;
	EntityDefinitionIdentifier() = default;
	EntityDefinitionIdentifier(const std::string& name) : mName(name) {}
	EntityDefinitionIdentifier(const std::string& ns, const std::string& name,
	                           const std::string& filter)
	    : mNamespace(ns), mName(name), mFilter(filter) {}
};

inline std::string EntityTypeToString(EntityType type) {
	switch (type) {
		case EntityType::Rabbit: return "rabbit";
		case EntityType::Husk: return "husk";
		case EntityType::Zombie: return "zombie";
		case EntityType::Skeleton: return "skeleton";
		case EntityType::Creeper: return "creeper";
		case EntityType::Spider: return "spider";
		case EntityType::Slime: return "slime";
		case EntityType::EnderMan: return "enderman";
		case EntityType::Witch: return "witch";
		case EntityType::Villager: return "villager";
		case EntityType::Cow: return "cow";
		case EntityType::Sheep: return "sheep";
		case EntityType::Pig: return "pig";
		case EntityType::Chicken: return "chicken";
		case EntityType::Horse: return "horse";
		case EntityType::Wolf: return "wolf";
		case EntityType::Ocelot: return "ocelot";
		case EntityType::Squid: return "squid";
		case EntityType::Bat: return "bat";
		default: return "unknown";
	}
}

class Mob;

// EntityClassTree subset — the biome registry only asks whether a
// spawn category belongs to a family; identity is enough here.
struct EntityClassTree {
	static bool isTypeInstanceOf(EntityType a, EntityType b) {
		return a == b;
	}
};

class MobSpawnerData : public WeighedRandom::WeighedRandomItem {
public:
	EntityDefinitionIdentifier mIdentifier;
	EntityType mobClassId;
	int minCount;
	int maxCount;
	bool featureMob = false;

	typedef std::function<void(std::vector<Mob*>&, Random&)> OnSpawnHerd;
	OnSpawnHerd mOnSpawnHerd = nullptr;
	typedef std::function<EntityDefinitionIdentifier(Random&)> OnSelectEntity;
	OnSelectEntity mOnSelectEntity = nullptr;

	MobSpawnerData() {}
	MobSpawnerData(EntityType mobClassId_, int probabilityWeight, int minCount_, int maxCount_,
	               OnSpawnHerd onSpawnHerd = nullptr, OnSelectEntity onSelectEntity = nullptr,
	               bool featureMob_ = false)
	    : WeighedRandom::WeighedRandomItem(probabilityWeight)
	    , mobClassId(mobClassId_)
	    , minCount(minCount_)
	    , maxCount(maxCount_)
	    , mOnSpawnHerd(onSpawnHerd)
	    , mOnSelectEntity(onSelectEntity)
	    , featureMob(featureMob_) {}
	MobSpawnerData(EntityType mobId, const std::string& fullDefinition, int probabilityWeight,
	               int minCount_, int maxCount_)
	    : WeighedRandom::WeighedRandomItem(probabilityWeight)
	    , mIdentifier(fullDefinition)
	    , mobClassId(mobId)
	    , minCount(minCount_)
	    , maxCount(maxCount_) {}
	MobSpawnerData(EntityType mobId, const EntityDefinitionIdentifier& ident, int probabilityWeight,
	               int minCount_, int maxCount_)
	    : WeighedRandom::WeighedRandomItem(probabilityWeight)
	    , mIdentifier(ident)
	    , mobClassId(mobId)
	    , minCount(minCount_)
	    , maxCount(maxCount_) {}
};

typedef std::vector<MobSpawnerData> MobList;

} // namespace vgen

