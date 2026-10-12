// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Synth — base class of the vanilla noise.
//
// Copiado 1:1 de `handheld/src/world/level/levelgen/synth/Synth.h`
// (SSMGAlt/PocketEdition): el `create()` que llena una rejilla evaluando
// getValue() por celda.
#pragma once

namespace cursestone::world::generator {

class Synth {
public:
    virtual ~Synth();

    int getDataSize(int width, int height);

    virtual float getValue(float x, float y) = 0;

    void create(int width, int height, float* result);
};

} // namespace cursestone::world::generator
