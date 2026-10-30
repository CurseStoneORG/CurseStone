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


#include "vgen/Vanilla.h"
#include "vgen/Vanilla.h"
#include "vgen/Vanilla.h"

namespace vgen {

class LevelChunk;
class Random;
class BiomeSouce;
class Vec3;
struct BlockID;
class Dimension;
class BlockVolume;

class LargeCaveFeature {
public:
	void apply(BlockVolume& blocks, LevelChunk& lc, Dimension& biomeSource, Random& random);

protected:
	void addRoom(BlockVolume& blocks, Random& random, LevelChunk& lc, const Vec3& room);

	bool _thinSand(buffer_span<BlockID> blocks, Height height, int y );

	bool isDiggable(BlockID block, BlockID above);

	void addTunnel(BlockVolume& blocks, Random& _random, LevelChunk& lc, const Vec3& startPos, float thickness, float yRot, float xRot, int step, int dist, float yScale);

	void addFeature(BlockVolume& blocks, LevelChunk& lc, Dimension& generator, Random& random, int x, int z );

};

} // namespace vgen

