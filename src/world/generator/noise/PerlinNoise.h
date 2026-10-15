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
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// PerlinNoise — la suma de octavas del vanilla: `levels` capas de
// ImprovedNoise, cada una al doble de frecuencia y a la mitad de amplitud
// (value += noise(x*pow, y*pow)/pow; pow /= 2).
//
// Copiado 1:1 de `handheld/src/world/level/levelgen/synth/PerlinNoise.h`
// (SSMGAlt/PocketEdition).
#pragma once

#include "world/generator/noise/MersenneRandom.h"
#include "world/generator/noise/Synth.h"

namespace cursestone::world::generator {

class ImprovedNoise;

class PerlinNoise : public Synth {
  public:
    explicit PerlinNoise(int levels);
    PerlinNoise(MersenneRandom* random, int levels);
    ~PerlinNoise() override;

    float getValue(float x, float y) override;
    float getValue(float x, float y, float z);

    float* getRegion(float* buffer, float x, float y, float z, int xSize, int ySize, int zSize,
                     float xScale, float yScale, float zScale);
    float* getRegion(float* sr, int x, int z, int xSize, int zSize, float xScale, float zScale,
                     float pow);

    int hashCode();

    // Anadido (no esta en el original): cuantas octavas tiene esta capa. El
    // vanilla suma las octavas SIN dividir por la amplitud total, asi que su
    // salida crece con `levels`; nuestro generador espera campos en [-1, 1] y
    // usa esto para normalizar.
    int levelCount() const {
        return levels;
    }

    // Optimizacion (NO cambia ni un bit): el puente del ruido vanilla
    // (noise/VanillaNoise.h) necesita estas dos constantes en CADA
    // evaluacion de campo, y antes las recalculeaba con un bucle por
    // llamada. Son puros valores de la capa, asi que se guardan una vez
    // en init() y se leen aqui. Mismos numeros, mismo orden de
    // sumatoria: la salida de getValue() no se toca.
    double amplitudeSum() const {
        return amp_sum_;
    }
    double dominantScale() const {
        return dom_scale_;
    }

  private:
    ImprovedNoise** noiseLevels;
    int levels;

    // Constantes precalculadas (ver accessors arriba). Identicas a las
    // que calculaba el bucle de layerAmplitudeSum()/layerDominantScale()
    // en VanillaNoise.h, solo que sin repetirlas por muestra.
    double amp_sum_ = 1.0;
    double dom_scale_ = 1.0;

    MersenneRandom _random;
    MersenneRandom* _rndPtr;

    void init(int levels);
};

} // namespace cursestone::world::generator
