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
#include "world/generator/noise/PerlinNoise.h"
#include "world/generator/noise/ImprovedNoise.h"

namespace cursestone::world::generator {

void PerlinNoise::init(int levels) {
    this->levels = levels;
    noiseLevels = new ImprovedNoise*[levels];
    for (int i = 0; i < levels; i++) {
        noiseLevels[i] = new ImprovedNoise(_rndPtr);
    }
    // Constantes del puente vanilla, precalculadas una vez (ver
    // PerlinNoise.h). Se replican AQUI los mismos bucles de
    // layerAmplitudeSum()/layerDominantScale() de VanillaNoise.h para que
    // los valores sean bit identicos a los que antes se calculaban en
    // cada evaluacion de campo.
    double s = 0.0, amp = 1.0;
    for (int i = 0; i < levels; ++i) {
        s += amp;
        amp *= 2.0;
    }
    amp_sum_ = s;
    dom_scale_ = (levels <= 0) ? 1.0 : (double)(1u << (levels - 1));
}

PerlinNoise::~PerlinNoise() {
    for (int i = 0; i < levels; ++i)
        delete noiseLevels[i];
    delete[] noiseLevels;
}

// El original usa Random(0) para su Random interno; el ruido siempre se
// construye con una semilla explicita (init() corre despues de fijar
// _rndPtr), asi que el valor inicial solo tiene que ser determinista.
PerlinNoise::PerlinNoise(int levels) : _random(0) {
    _rndPtr = &_random;
    init(levels);
}

PerlinNoise::PerlinNoise(MersenneRandom* random, int levels) : _random(0) {
    _rndPtr = random;
    init(levels);
}

float PerlinNoise::getValue(float x, float y) {
    float value = 0;
    float pow = 1;

    for (int i = 0; i < levels; i++) {
        value += noiseLevels[i]->getValue(x * pow, y * pow) / pow;
        pow /= 2;
    }

    return value;
}

float PerlinNoise::getValue(float x, float y, float z) {
    float value = 0;
    float pow = 1;

    for (int i = 0; i < levels; i++) {
        value += noiseLevels[i]->getValue(x * pow, y * pow, z * pow) / pow;
        pow /= 2;
    }

    return value;
}

float* PerlinNoise::getRegion(float* buffer, float x, float y, float z, int xSize, int ySize,
                              int zSize, float xScale, float yScale, float zScale) {
    const int size = xSize * ySize * zSize;
    if (buffer == 0) {
        buffer = new float[size];
    }
    for (int i = 0; i < size; i++)
        buffer[i] = 0;

    float pow = 1;

    for (int i = 0; i < levels; i++) {
        noiseLevels[i]->add(buffer, x, y, z, xSize, ySize, zSize, xScale * pow, yScale * pow,
                            zScale * pow, pow);
        pow /= 2;
    }

    return buffer;
}

float* PerlinNoise::getRegion(float* sr, int x, int z, int xSize, int zSize, float xScale,
                              float zScale, float pow) {
    return getRegion(sr, (float)x, 10.0f, (float)z, xSize, 1, zSize, xScale, 1, zScale);
}

int PerlinNoise::hashCode() {
    int x = 4711;
    for (int i = 0; i < levels; ++i)
        x *= noiseLevels[i]->hashCode();
    return x;
}

} // namespace cursestone::world::generator
