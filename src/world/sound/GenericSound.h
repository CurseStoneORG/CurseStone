#pragma once
#include "Sound.h"
namespace cursestone::world::sound {
class GenericSound : public Sound {
public:
    GenericSound(SoundId id, Position pos, float pitch = 1.0f, float volume = 1.0f)
        : Sound(id, pos, pitch, volume) {}
};
} // namespace
