// SPDX-License-Identifier: LGPL-3.0-or-later
// PopulatorFeatures — small decorators exposed as Populators.
// Ported/adapted from NetherGamesMC/ext-vanillagenerator decorators.
#pragma once
#include "Populator.h"
#include <cstdint>

namespace cursestone::world::generator::populator {

class PopulatorCactus : public Populator {
public:
    explicit PopulatorCactus(int attempts) : attempts_(attempts) {}
    const char* name() const override { return "cactus"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
};

class PopulatorSugarCane : public Populator {
public:
    explicit PopulatorSugarCane(int attempts) : attempts_(attempts) {}
    const char* name() const override { return "sugarcane"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
};

class PopulatorDeadBush : public Populator {
public:
    explicit PopulatorDeadBush(int attempts) : attempts_(attempts) {}
    const char* name() const override { return "deadbush"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
};

class PopulatorMushroom : public Populator {
public:
    PopulatorMushroom(int attempts, uint8_t id) : attempts_(attempts), id_(id) {}
    const char* name() const override { return "mushroom"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
    uint8_t id_;
};

class PopulatorPumpkin : public Populator {
public:
    explicit PopulatorPumpkin(int attempts) : attempts_(attempts) {}
    const char* name() const override { return "pumpkin"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
};

class PopulatorMelon : public Populator {
public:
    explicit PopulatorMelon(int attempts) : attempts_(attempts) {}
    const char* name() const override { return "melon"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
};

class PopulatorDoublePlant : public Populator {
public:
    PopulatorDoublePlant(int attempts, uint8_t data) : attempts_(attempts), data_(data) {}
    const char* name() const override { return "double_plant"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
    uint8_t data_;
};

class PopulatorLake : public Populator {
public:
    PopulatorLake(int attempts, uint8_t liquid) : attempts_(attempts), liquid_(liquid) {}
    const char* name() const override { return "lake"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
    uint8_t liquid_;
};

// Very rare lava pools on flat plains. The pool is a shallow lava puddle
// ringed with stone; the populator rolls once per chunk, so pools are an
// event rather than a second biome.
class PopulatorLavaPool : public Populator {
public:
    explicit PopulatorLavaPool(int attempts) : attempts_(attempts) {}
    const char* name() const override { return "lavapool"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
};

class PopulatorWaterLily : public Populator {
public:
    explicit PopulatorWaterLily(int attempts) : attempts_(attempts) {}
    const char* name() const override { return "waterlily"; }
    void populate(GenerationContext& ctx) override;
private:
    int attempts_;
};

} // namespace cursestone::world::generator::populator
