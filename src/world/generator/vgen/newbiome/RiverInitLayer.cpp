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

#include "vgen/newbiome/RiverInitLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

RiverInitLayer::RiverInitLayer(RandomSeed seedMixup, LayerPtr& parent)
	: Layer(seedMixup) {
	mParent = parent;
}

void RiverInitLayer::fillArea(LayerData& layerData, int xo, int yo, int w, int h) {
	mParent->fillArea(layerData, xo, yo, w, h);

	for(auto y : range(h)) {
		for(auto x : range(w)) {
			initRandom(x + xo, y + yo);
			layerData.mResult[x + y * w] = layerData.mParentArea[x + y * w] > 0 ? nextRandom(299999) + 2 : 0;
		}
	}
	layerData.swap();
}

} // namespace vgen

