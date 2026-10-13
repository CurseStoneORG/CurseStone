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
#include "Generator.h"
#include "world/generator/TerrainShape.h"
#include "world/generator/populator/Populator.h"
#include <vector>
#include <memory>
namespace cursestone::world::generator {
class Normal : public Generator {
public:
    explicit Normal(int64_t seed);
    ~Normal() override;
    const char* name() const override { return "normal"; }
    void generate(GenerationContext& ctx) override;
    void populate(GenerationContext& ctx) override;
    // Mirrors Nukkit Normal.getSpawn(): (127.5, 128, 127.5)
    mcp_vector3_t getSpawn() const override {
        return mcp_v3(127.5, 128.0, 127.5);
    }
private:
    int64_t seed_;
    // Terrain shape + climate + rivers, shared with the feature populators
    // through TerrainShape so trees can span chunk borders.
    TerrainNoise noise_;
    // Ruido PROVISIONAL cuando generate() corre con una semilla distinta
    // a la del ctor (no ocurre en el servidor: World construye el generador
    // con la semilla final del provider). Vive como thread_local DENTRO de
    // generate() y no como miembro: generate()/populate() corren en varios
    // workers del AsyncPool a la vez sobre el mismo Generator, y un miembro
    // compartido se reconstruye (libera) debajo de los pies del otro worker
    // (era el SIGSEGV en PerlinNoise::getValue). Con la semilla de siempre,
    // ctx.noise_cache apunta a noise_ — sin costo.
    std::vector<std::unique_ptr<populator::Populator>> populators_;
};
} // namespace
