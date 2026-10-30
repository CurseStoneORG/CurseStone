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

#include "vgen/newbiome/Layer.h"

namespace vgen {

class ZoomLayer : public Layer {
public:

	static const int ZOOM_BITS = 1;
	static const int ZOOM_OFFSET = 1;
	static const int ZOOM_MASK = 1 << (ZOOM_BITS - 1);

	ZoomLayer(RandomSeed seedMixup, LayerPtr parent);
	virtual void fillArea(LayerData& layerData, int xo, int yo, int w, int h) override;
	static LayerPtr zoom(RandomSeed seed, LayerPtr& sup, int count);

};

} // namespace vgen

