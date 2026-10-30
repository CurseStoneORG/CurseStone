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

#include "vgen/newbiome/Layer.h"

#include "vgen/biome/Biome.h"
#include "vgen/Vanilla.h"
#include "vgen/newbiome/AddDeepOceanLayer.h"
#include "vgen/newbiome/AddEdgeLayer.h"
#include "vgen/newbiome/AddIslandLayer.h"
#include "vgen/newbiome/AddMushroomIslandLayer.h"
#include "vgen/newbiome/AddSnowLayer.h"
#include "vgen/newbiome/BiomeInitLayer.h"
#include "vgen/newbiome/BiomeEdgeLayer.h"
#include "vgen/newbiome/IslandLayer.h"
#include "vgen/newbiome/FuzzyZoomLayer.h"
#include "vgen/newbiome/RareBiomeSpotLayer.h"
#include "vgen/newbiome/RegionHillsLayer.h"
#include "vgen/newbiome/RemoveTooMuchOceanLayer.h"
#include "vgen/newbiome/RiverInitLayer.h"
#include "vgen/newbiome/RiverLayer.h"
#include "vgen/newbiome/RiverMixerLayer.h"
#include "vgen/newbiome/ShoreLayer.h"
#include "vgen/newbiome/SmoothLayer.h"
#include "vgen/newbiome/VoronoiZoom.h"

namespace vgen {

void LayerData::swap() {
        std::swap(mParentArea, mResult);
}

void Layer::getDefaultLayers(int64_t seed, LayerPtr& biomeLayer, LayerPtr& zoomedLayer, GeneratorType genType) {
        LayerPtr islandLayer = make_shared<IslandLayer>(1);
        islandLayer = make_shared<FuzzyZoomLayer>(2000, islandLayer);
        islandLayer = make_shared<AddIslandLayer>(1, islandLayer);
        islandLayer = make_shared<ZoomLayer>(2001, islandLayer);
        islandLayer = make_shared<AddIslandLayer>(2, islandLayer);
        islandLayer = make_shared<AddIslandLayer>(50, islandLayer);
        islandLayer = make_shared<AddIslandLayer>(70, islandLayer);
        islandLayer = make_shared<RemoveTooMuchOceanLayer>(2, islandLayer);
        islandLayer = make_shared<AddSnowLayer>(2, islandLayer);
        islandLayer = make_shared<AddIslandLayer>(3, islandLayer);
        islandLayer = make_shared<AddEdgeLayer>(2, islandLayer, AddEdgeLayer::Mode::COOL_WARM);
        islandLayer = make_shared<AddEdgeLayer>(2, islandLayer, AddEdgeLayer::Mode::HEAT_ICE);
        islandLayer = make_shared<AddEdgeLayer>(3, islandLayer, AddEdgeLayer::Mode::SPECIAL);
        islandLayer = make_shared<ZoomLayer>(2002, islandLayer);
        islandLayer = make_shared<ZoomLayer>(2003, islandLayer);
        islandLayer = make_shared<AddIslandLayer>(4, islandLayer);
        islandLayer = make_shared<AddMushroomIslandLayer>(5, islandLayer);
        islandLayer = make_shared<AddDeepOceanLayer>(4, islandLayer);
        islandLayer = ZoomLayer::zoom(1000, islandLayer, 0);
        //        }

        //        int zoomLevel = 0;
        int zoomLevel = 4;

        if (genType == GeneratorType::Legacy) {
                zoomLevel = 2;
        }

        LayerPtr riverLayer = islandLayer;
        riverLayer = ZoomLayer::zoom(1000, riverLayer, 0);
        riverLayer = make_shared<RiverInitLayer>(100, riverLayer);

        biomeLayer = islandLayer;
        biomeLayer = make_shared<BiomeInitLayer>(200, biomeLayer, genType);

        biomeLayer = ZoomLayer::zoom(1000, biomeLayer, 2);
        biomeLayer = make_shared<BiomeEdgeLayer>(1000, biomeLayer, genType == GeneratorType::Overworld);

        LayerPtr biomeRiverLayer = riverLayer;
        biomeRiverLayer = ZoomLayer::zoom(1000, biomeRiverLayer, 2);
        biomeLayer = make_shared<RegionHillsLayer>(1000, biomeLayer, biomeRiverLayer);

        riverLayer = ZoomLayer::zoom(1000, riverLayer, 2);
        riverLayer = ZoomLayer::zoom(1000, riverLayer, zoomLevel);
        riverLayer = make_shared<RiverLayer>(1, riverLayer);
        riverLayer = make_shared<SmoothLayer>(1000, riverLayer);

        biomeLayer = make_shared<RareBiomeSpotLayer>(1001, biomeLayer);

        for(auto i : range(zoomLevel)) {
                biomeLayer = make_shared<ZoomLayer>(1000 + i, biomeLayer);
                if(i == 0) {
                        biomeLayer = make_shared<AddIslandLayer>(3, biomeLayer);
                }

                if(i == 1) {
                        biomeLayer = make_shared<ShoreLayer>(1000, biomeLayer);
                }
        }

        biomeLayer = make_shared<SmoothLayer>(1000, biomeLayer);

        biomeLayer = make_shared<RiverMixerLayer>(100, biomeLayer, riverLayer);

        LayerPtr debugLayer = biomeLayer;
        zoomedLayer = make_shared<VoronoiZoom>(10, biomeLayer);

        biomeLayer->init(seed);
        zoomedLayer->init(seed);
}

Layer::~Layer() {

}

Layer::Layer(RandomSeed seedMixup) {
        mSeedMixup = seedMixup;
        mSeedMixup *= mSeedMixup * 6364136223846793005l + 1442695040888963407l;
        mSeedMixup += seedMixup;
        mSeedMixup *= mSeedMixup * 6364136223846793005l + 1442695040888963407l;
        mSeedMixup += seedMixup;
        mSeedMixup *= mSeedMixup * 6364136223846793005l + 1442695040888963407l;
        mSeedMixup += seedMixup;
}

void Layer::init(int64_t seed) {
        mSeed = seed;
        if(mParent != nullptr) {
                mParent->init(seed);
        }
        mSeed *= mSeed * 6364136223846793005l + 1442695040888963407l;
        mSeed += mSeedMixup;
        mSeed *= mSeed * 6364136223846793005l + 1442695040888963407l;
        mSeed += mSeedMixup;
        mSeed *= mSeed * 6364136223846793005l + 1442695040888963407l;
        mSeed += mSeedMixup;
}

// Per-thread random chain state. The vanilla layers re-seed this state at
// the start of every output cell, so each cell's draw chain is
// self-contained; keeping it thread-local lets concurrent workers generate
// chunks without corrupting each other's biome draws.
static thread_local int64_t t_layerRval = 0;

void Layer::initRandom(int64_t x, int64_t y) {
        t_layerRval = mSeed;
        t_layerRval *= t_layerRval * 6364136223846793005l + 1442695040888963407l;
        t_layerRval += x;
        t_layerRval *= t_layerRval * 6364136223846793005l + 1442695040888963407l;
        t_layerRval += y;
        t_layerRval *= t_layerRval * 6364136223846793005l + 1442695040888963407l;
        t_layerRval += x;
        t_layerRval *= t_layerRval * 6364136223846793005l + 1442695040888963407l;
        t_layerRval += y;
}

int Layer::nextRandom(int max) {
        int result = (int)((t_layerRval >> 24) % max);
        if(result < 0) {
                result += max;
        }
        t_layerRval *= t_layerRval * 6364136223846793005l + 1442695040888963407l;
        t_layerRval += mSeed;
        return result;
}

bool Layer::_isSame(const int a, const int b) {
        if(a == b) {
                return true;
        }
        if(a == Biome::mesaRock->mId || a == Biome::mesaClearRock->mId) {
                return b == Biome::mesaRock->mId || b == Biome::mesaClearRock->mId;
        }

        Biome* biome1 = Biome::getBiome(a);
        Biome* biome2 = Biome::getBiome(b);

        if(biome1 != nullptr && biome2 != nullptr) {
                return biome1->isSame(biome2);
        }

        return false;
}

bool Layer::_isOcean(int id) {
        return id == Biome::ocean->mId || id == Biome::deepOcean->mId || id == Biome::frozenOcean->mId;
}

int Layer::_random(int* list, int count) {
        return list[nextRandom(count)];
}

int Layer::_modeOrRandom(const int a, const int b, const int c, const int d) {
        if(b == c && c == d) {
                return b;
        }
        if(a == b && a == c) {
                return a;
        }
        if(a == b && a == d) {
                return a;
        }
        if(a == c && a == d) {
                return a;
        }
        if(a == b && c != d) {
                return a;
        }
        if(a == c && b != d) {
                return a;
        }
        if(a == d && b != c) {
                return a;
        }
        if(b == c && a != d) {
                return b;
        }
        if(b == d && a != c) {
                return b;
        }
        if(c == d && a != b) {
                return c;
        }

        int randomSeed[] = {
                a, b, c, d
        };
        return _random(randomSeed, 4);
}

} // namespace vgen

