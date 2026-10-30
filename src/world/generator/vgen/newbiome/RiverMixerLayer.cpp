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

#include "vgen/newbiome/RiverMixerLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

RiverMixerLayer::RiverMixerLayer(RandomSeed seedMixup, LayerPtr& biomes, LayerPtr& rivers)
	: Layer(seedMixup)
	, mBiomes(biomes)
	, mRivers(rivers) {
	mParent = mParent;
}

void RiverMixerLayer::init(int64_t seed) {
	mBiomes->init(seed);
	mRivers->init(seed);
	Layer::init(seed);
}

void RiverMixerLayer::fillArea(LayerData& layerData, int xo, int yo, int w, int h) {
	mBiomes->fillArea(layerData, xo, yo, w, h);
	LayerData riverData;

	mRivers->fillArea(riverData, xo, yo, w, h);

	for(auto i : range(w * h)) {
		if(layerData.mParentArea[i] == Biome::ocean->mId || layerData.mParentArea[i] == Biome::deepOcean->mId) {
			layerData.mResult[i] = layerData.mParentArea[i];
		}
		else {
			if(riverData.mParentArea[i] == Biome::river->mId) {
				if(layerData.mParentArea[i] == Biome::iceFlats->mId) {
					layerData.mResult[i] = Biome::frozenRiver->mId;
				}
				else if(layerData.mParentArea[i] == Biome::mushroomIsland->mId || layerData.mParentArea[i] == Biome::mushroomIslandShore->mId) {
					layerData.mResult[i] = Biome::mushroomIslandShore->mId;
				}
				else {
					layerData.mResult[i] = riverData.mParentArea[i] & 0xff;
				}
			}
			else {
				layerData.mResult[i] = layerData.mParentArea[i];
			}
		}
	}
	layerData.swap();
}

} // namespace vgen

