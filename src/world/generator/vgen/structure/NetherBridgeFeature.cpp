/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
#include "vgen/structure/NetherBridgeFeature.h"

#include "vgen/structure/NetherBridgePieces.h"
#include "vgen/biome/BiomeSource.h"

namespace vgen {

NetherBridgeFeature::NetherBridgeFeature() :
	StructureFeature() {
	bridgeEnemies.insert(bridgeEnemies.end(), MobSpawnerData(EntityType::Blaze, 10, 2, 3, nullptr, nullptr, true));
	bridgeEnemies.insert(bridgeEnemies.end(), MobSpawnerData(EntityType::PigZombie, 5, 4, 4, nullptr, nullptr, true));
	bridgeEnemies.insert(bridgeEnemies.end(), MobSpawnerData(EntityType::LavaSlime, 3, 4, 4, nullptr, nullptr, true));
	bridgeEnemies.insert(bridgeEnemies.end(), MobSpawnerData(EntityType::Skeleton, 10, 4, 4, nullptr, nullptr, true));
}

std::string NetherBridgeFeature::getFeatureName() {
	return "Fortress";
}

NetherBridgeFeature::MobList& NetherBridgeFeature::getBridgeEnemies() {
	return bridgeEnemies;
}

bool NetherBridgeFeature::isFeatureChunk(BiomeSource* biomeSource, Random& random, const ChunkPos& chunkPos) {

	//TODO refactor and use a RWLock
	LockGuard l(positionMutex);

	int cx = chunkPos.x >> 4;
	int cz = chunkPos.z >> 4;

	random.setSeed(cx ^ (cz << 4) ^ biomeSource->getSeed());
	random.nextInt();

	if (random.nextInt(3) != 0) {
		return false;
	}
	if (chunkPos.x != ((cx << 4) + 4 + random.nextInt(8))) {
		return false;
	}
	if (chunkPos.z != ((cz << 4) + 4 + random.nextInt(8))) {
		return false;
	}
	return true;
}

Unique<StructureStart> NetherBridgeFeature::createStructureStart(Dimension& generator, Random& random, const ChunkPos& cp) {
	return make_unique<NetherBridgeStart>(generator, random, cp.x, cp.z);
}

NetherBridgeStart::NetherBridgeStart() :
	StructureStart(0, 0) {
}

NetherBridgeStart::NetherBridgeStart(Dimension& biomeSource, Random& random, int chunkX, int chunkZ) :
	StructureStart(chunkX, chunkZ) {
	pieces.push_back(make_unique<NBStartPiece>(random, (chunkX << 4) + 2, (chunkZ << 4) + 2));

	auto start = (NBStartPiece*)pieces.back().get();
	start->addChildren(start, pieces, random);

	ReferencedPieceList& pendingChildren = start->pendingChildren;

	while (!pendingChildren.empty()) {
		int pos = random.nextInt(pendingChildren.size());
		StructurePiece* structurePiece = pendingChildren[pos];
		pendingChildren.erase(pendingChildren.begin() + pos);
		structurePiece->addChildren(start, pieces, random);
	}

	calculateBoundingBox();
	moveInsideHeights(&random, 48, 70);
}

} // namespace vgen
