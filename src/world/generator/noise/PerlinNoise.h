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

    float* getRegion(float* buffer, float x, float y, float z, int xSize,
                     int ySize, int zSize, float xScale, float yScale,
                     float zScale);
    float* getRegion(float* sr, int x, int z, int xSize, int zSize, float xScale,
                     float zScale, float pow);

    int hashCode();

    // Anadido (no esta en el original): cuantas octavas tiene esta capa. El
    // vanilla suma las octavas SIN dividir por la amplitud total, asi que su
    // salida crece con `levels`; nuestro generador espera campos en [-1, 1] y
    // usa esto para normalizar.
    int levelCount() const { return levels; }

private:
    ImprovedNoise** noiseLevels;
    int levels;

    MersenneRandom _random;
    MersenneRandom* _rndPtr;

    void init(int levels);
};

} // namespace cursestone::world::generator
