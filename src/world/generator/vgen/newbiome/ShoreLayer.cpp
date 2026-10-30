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

#include "vgen/newbiome/ShoreLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

ShoreLayer::ShoreLayer(RandomSeed seedMixup, LayerPtr& parent)
	: Layer(seedMixup) {
	mParent = parent;
}

void ShoreLayer::fillArea(LayerData& layerData, int xo, int yo, int w, int h) {
	mParent->fillArea(layerData, xo - 1, yo - 1, w + 2, h + 2);

	for(auto y : range(h)) {
		for(auto x : range(w)) {
			initRandom(x + xo, y + yo);
			int old = layerData.mParentArea[(x + 1) + (y + 1) * (w + 2)];
			Biome* biome = Biome::getBiome(old);
			if(old == Biome::mushroomIsland->mId) {
				const int _n = layerData.mParentArea[(x + 1) + (y + 1 - 1) * (w + 2)];
				const int _e = layerData.mParentArea[(x + 1 + 1) + (y + 1) * (w + 2)];
				const int _w = layerData.mParentArea[(x + 1 - 1) + (y + 1) * (w + 2)];
				const int _s = layerData.mParentArea[(x + 1) + (y + 1 + 1) * (w + 2)];
				if(_n == Biome::ocean->mId || _e == Biome::ocean->mId || _w == Biome::ocean->mId || _s == Biome::ocean->mId) {
					layerData.mResult[x + y * w] = Biome::mushroomIslandShore->mId;
				}
				else {
					layerData.mResult[x + y * w] = old;
				}
			}
			else if(biome != nullptr && biome->getBiomeType() == Biome::BiomeType::Jungle) {
				const int _n = layerData.mParentArea[(x + 1) + (y + 1 - 1) * (w + 2)];
				const int _e = layerData.mParentArea[(x + 1 + 1) + (y + 1) * (w + 2)];
				const int _w = layerData.mParentArea[(x + 1 - 1) + (y + 1) * (w + 2)];
				const int _s = layerData.mParentArea[(x + 1) + (y + 1 + 1) * (w + 2)];
				if(!_isJungleCompatible(_n) || !_isJungleCompatible(_e) || !_isJungleCompatible(_w) || !_isJungleCompatible(_s)) {
					layerData.mResult[x + y * w] = Biome::jungleEdge->mId;
				}
				else if(_isOcean(_n) || _isOcean(_e) || _isOcean(_w) || _isOcean(_s)) {
					layerData.mResult[x + y * w] = Biome::beaches->mId;
				}
				else {
					layerData.mResult[x + y * w] = old;
				}
			}
			else if(old == Biome::extremeHills->mId || old == Biome::extremeHillsWithTrees->mId || old == Biome::smallerExtremeHills->mId) {
				_replaceIfNeighborOcean(layerData, x, y, w, old, Biome::stoneBeach->mId);
			}
			else if(biome != nullptr && biome->isSnowCovered()) {
				_replaceIfNeighborOcean(layerData, x, y, w, old, Biome::coldBeach->mId);
			}
			else if(old == Biome::mesa->mId || old == Biome::mesaRock->mId) {
				const int _n = layerData.mParentArea[(x + 1) + (y + 1 - 1) * (w + 2)];
				const int _e = layerData.mParentArea[(x + 1 + 1) + (y + 1) * (w + 2)];
				const int _w = layerData.mParentArea[(x + 1 - 1) + (y + 1) * (w + 2)];
				const int _s = layerData.mParentArea[(x + 1) + (y + 1 + 1) * (w + 2)];
				if(_isOcean(_n) || _isOcean(_e) || _isOcean(_w) || _isOcean(_s)) {
					layerData.mResult[x + y * w] = old;
				}
				else if(!_isMesa(_n) || !_isMesa(_e) || !_isMesa(_w) || !_isMesa(_s)) {
					layerData.mResult[x + y * w] = Biome::desert->mId;
				}
				else {
					layerData.mResult[x + y * w] = old;
				}
			}
			else if(old != Biome::ocean->mId && old != Biome::deepOcean->mId && old != Biome::river->mId && old != Biome::swampland->mId) {
				const int _n = layerData.mParentArea[(x + 1) + (y + 1 - 1) * (w + 2)];
				const int _e = layerData.mParentArea[(x + 1 + 1) + (y + 1) * (w + 2)];
				const int _w = layerData.mParentArea[(x + 1 - 1) + (y + 1) * (w + 2)];
				const int _s = layerData.mParentArea[(x + 1) + (y + 1 + 1) * (w + 2)];
				if(_isOcean(_n) || _isOcean(_e) || _isOcean(_w) || _isOcean(_s)) {
					layerData.mResult[x + y * w] = Biome::beaches->mId;
				}
				else {
					layerData.mResult[x + y * w] = old;
				}
			}
			else {
				layerData.mResult[x + y * w] = old;

			}
		}
	}
	layerData.swap();
}

void ShoreLayer::_replaceIfNeighborOcean(LayerData& layerData, int x, int y, int w, int old, int replacement) {
	if(_isOcean(old)) {
		layerData.mResult[x + y * w] = old;
		return;
	}
	const int _n = layerData.mParentArea[(x + 1) + (y + 1 - 1) * (w + 2)];
	const int _e = layerData.mParentArea[(x + 1 + 1) + (y + 1) * (w + 2)];
	const int _w = layerData.mParentArea[(x + 1 - 1) + (y + 1) * (w + 2)];
	const int _s = layerData.mParentArea[(x + 1) + (y + 1 + 1) * (w + 2)];
	if(_isOcean(_n) || _isOcean(_e) || _isOcean(_w) || _isOcean(_s)) {
		layerData.mResult[x + y * w] = replacement;
	}
	else {
		layerData.mResult[x + y * w] = old;
	}
}

bool ShoreLayer::_isJungleCompatible(int id) {
	if(Biome::getBiome(id) != nullptr && Biome::getBiome(id)->getBiomeType() == Biome::BiomeType::Jungle) {
		return true;
	}

	return !(id != Biome::jungleEdge->mId && id != Biome::jungle->mId && id != Biome::jungleHills->mId && id != Biome::forest->mId && id != Biome::taiga->mId && !_isOcean(id));
}

bool ShoreLayer::_isMesa(int id) {
	return Biome::getBiome(id) != nullptr && Biome::getBiome(id)->getBiomeType() == Biome::BiomeType::Mesa;
}

} // namespace vgen

