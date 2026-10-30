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

#include "vgen/newbiome/FuzzyZoomLayer.h"

#include "vgen/biome/Biome.h"

namespace vgen {

FuzzyZoomLayer::FuzzyZoomLayer(RandomSeed seedMixup, LayerPtr& parent) 
	: ZoomLayer(seedMixup, parent) {

}

LayerPtr FuzzyZoomLayer::zoom(RandomSeed seed, LayerPtr& sup, int count) {
	LayerPtr result = sup;

	for(auto i : range(count)) {
		result = make_shared<FuzzyZoomLayer>(seed + i, result);
	}
	return result;
}

int FuzzyZoomLayer::_modeOrRandom(const int a, const int b, const int c, const int d) {
	int randomData[] = {
		a, b, c, d
	};
	return _random(randomData, 4);
}

} // namespace vgen

