/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */

#include "vgen/VgenWorld.h"

#include "vgen/structure/StructureFeature.h"
#include "vgen/structure/StructureStart.h"
#include "vgen/structure/StructurePiece.h"

#include "vgen/VgenWorld.h"
#include "vgen/Vanilla.h"
#include "vgen/VgenWorld.h"

#include "vgen/structure/MineshaftFeature.h"
#include "vgen/structure/RandomScatteredLargeFeature.h"
#include "vgen/structure/VillageFeature.h"
#include "vgen/VgenTags.h"

namespace vgen {

void StructureFeature::addFeature(LevelChunk& lc, Dimension& generator, Random& random, int x, int z ){
	// restoreSavedData(level);

	// this method is called for each chunk within 8 chunk's distance from
	// the chunk being generated, but not all chunks are the sources of
	// structures

	// clear random key
	random.nextInt();
	ChunkPos cp(x, z);

	if (!isFeatureChunk(&generator.getBiomes(), random, cp)) {
		return;
	}

	{
		LockGuard l(cacheMutex);
		if (cachedStructures.count(cp) == 0) {
			cachedStructures[cp] = createStructureStart(generator, random, cp);
		}
	}
}

bool StructureFeature::postProcess(BlockSource* level, Random& random, int chunkX, int chunkZ) {

	LockGuard l(cacheMutex);

	//restoreSavedData(level);

	int cx = (chunkX << 4);	// +8;
	int cz = (chunkZ << 4);	// +8;

	bool intersection = false;

	for (auto& pair : cachedStructures) {
		Unique<StructureStart>& start = pair.second;
		if (start->isValid()) {
			//if (start->getBoundingBox().intersects(cx - 16, cz - 16, cx + 32, cz + 32))
			if (start->getBoundingBox().intersects(cx, cz, cx + 16, cz + 16)) {

				//for (int cxo = chunkX - 1; cxo <= chunkX + 1; cxo++)
				{
					int cxo = chunkX;
					//for (int czo = chunkZ - 1; czo <= chunkZ + 1; czo++)
					{
						int czo = chunkZ;
						int chunkHash = ChunkPos::hashCode(cxo, czo);
						bool alreadyDone = false;

						for (unsigned int i = 0; i < start->generatedChunkPositions.size(); i++) {
							if (chunkHash == start->generatedChunkPositions[i]) {
								alreadyDone = true;
								break;
							}
						}
						if (!alreadyDone) {
							intersection |= start->postProcess(level, random, BoundingBox((cxo << 4), (czo << 4), (cxo << 4) + 15, (czo << 4) + 15));
							start->generatedChunkPositions.push_back(chunkHash);
						}
					}
				}

				// because some feature pieces are modified in the postProcess step, we need to save them again
				// if (intersection) {
				//saveFeature(structureStart.getChunkX(), structureStart.getChunkZ(), structureStart);
				// }
			}
		}
	}
	return intersection;
}

void StructureFeature::postProcessMobsAt(BlockSource* blockSource, int chunkWestBlock, int chunkNorthBlock, Random& random){
	LockGuard l(cacheMutex);

	//restoreSavedData(level);

	BoundingBox bb(chunkWestBlock, chunkNorthBlock, chunkWestBlock + 16, chunkNorthBlock + 16);

	for (auto& pair : cachedStructures) {
		auto& start = pair.second;
		if (start->isValid()) {
			if (start->getBoundingBox().intersects(bb)) {
				start->postProcessMobsAt(blockSource, random, bb);
			}
		}
	}
}

bool StructureFeature::isInsideFeature(int cellX, int cellY, int cellZ) {
	//restoreSavedData(level);
	return getStructureAt(cellX, cellY, cellZ) != nullptr;
}

StructureStart* StructureFeature::getStructureAt(int cellX, int cellY, int cellZ) {
	for (auto& pair : cachedStructures) {
		auto& start = pair.second;
		if (start->isValid()) {
			if (start->getBoundingBox().intersects(cellX, cellZ, cellX, cellZ)) {

				PieceList::const_iterator it = start->getPieces().cbegin();

				while (it != start->getPieces().cend()) {
					const Unique<StructurePiece>& next = (*it);
					if (next->getBoundingBox().isInside(cellX, cellY, cellZ)) {
						return start.get();
					}
					++it;
				}
			}
		}
	}
	return nullptr;
}

std::vector<BlockPos> StructureFeature::getGuesstimatedFeaturePositions() {
	return std::vector<BlockPos>();
}

bool StructureFeature::isInsideBoundingFeature(int cellX, int cellY, int cellZ) {

	//restoreSavedData(level);

	for (auto& pair : cachedStructures) {
		auto& start = pair.second;
		if (start->isValid()) {
			return start->getBoundingBox().intersects(cellX, cellZ, cellX, cellZ);
		}
	}
	return false;
}

bool StructureFeature::getNearestGeneratedFeature(Player& player, BlockPos& pos) {
	return false;
}



// Yeah, I know. This sucks, but ZBR was two days ago.
static Unique<StructureStart> createStructure(StructureFeatureType type) {
	switch (type) {
	case StructureFeatureType::Village:
		return make_unique<VillageStart>();

	case StructureFeatureType::Temple:
		return make_unique<ScatteredFeatureStart>();

	case StructureFeatureType::Mineshaft:
		return make_unique<MineshaftStart>();

	default:
		break;
	}
	// Can't return StructureStart as it's pure virtual :(
	DEBUG_ASSERT(false, "You forgot to add your new structure type to createStructure");
	return make_unique<VillageStart>();
}


} // namespace vgen

