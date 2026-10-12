// SPDX-License-Identifier: LGPL-3.0-or-later
//
// ImprovedNoise — el ruido de Perlin "mejorado" del vanilla (el mismo
// algoritmo que Ken Perlin publico en 2002 y que Minecraft usa).
//
// Copiado 1:1 de `handheld/src/world/level/levelgen/synth/ImprovedNoise.h`
// (SSMGAlt/PocketEdition). Los offsets xo/yo/zo y la tabla p[512] salen del
// MersenneRandom, exactamente como en el original.
#pragma once

#include "world/generator/noise/Synth.h"

namespace cursestone::world::generator {

class MersenneRandom;

class ImprovedNoise : public Synth {
public:
    ImprovedNoise();

    explicit ImprovedNoise(MersenneRandom* random);

    void init(MersenneRandom* random);

    float noise(float _x, float _y, float _z);

    float lerp(float t, float a, float b) const;

    float grad2(int hash, float x, float z) const;
    float grad(int hash, float x, float y, float z) const;

    float getValue(float x, float y) override;
    float getValue(float x, float y, float z);

    void add(float* buffer, float _x, float _y, float _z, int xSize, int ySize,
             int zSize, float xs, float ys, float zs, float pow);

    int hashCode();

    float scale;
    float xo, yo, zo;

private:
    int p[512];
};

} // namespace cursestone::world::generator
