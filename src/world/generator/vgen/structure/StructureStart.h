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

#pragma once

#include "vgen/structure/BoundingBox.h"
#include "vgen/structure/StructurePiece.h"

namespace vgen {

class Level;
class Random;
class CompoundTag;
enum class StructureFeatureType : int8_t;

class StructureStart {
public:

	StructureStart(int x, int z)
		: chunkX(x)
		, chunkZ(z){

	}

	virtual ~StructureStart() {
	}

	virtual bool postProcess(BlockSource* level, Random& random, const BoundingBox& chunkBB);
	void postProcessMobsAt(BlockSource* blockSource, Random& random, const BoundingBox& chunkBB);

	virtual bool isValid() const {
		return true;
	}

	int getChunkX() const {
		return chunkX;
	}

	int getChunkZ() const {
		return chunkZ;
	}

	const BoundingBox& getBoundingBox() const {
		return boundingBox;
	}

	const PieceList& getPieces() const {
		return pieces;
	}

	virtual StructureFeatureType getType() const = 0;

protected:

	void calculateBoundingBox();
	void moveToLevel(Random& random, int maxY);
	void moveInsideHeights(Random* random, int lowestAllowed, int highestAllowed);

	BoundingBox boundingBox;
	int chunkX, chunkZ;
	PieceList pieces;

public:

	std::vector<int> generatedChunkPositions;

};

} // namespace vgen

