// SPDX-License-Identifier: LGPL-3.0-or-later
// GenRandom — deterministic world-generation PRNG.
//
// Ported 1:1 from NetherGamesMC/ext-vanillagenerator's Random class so the
// cave/tree/decorator placement matches the reference generator. It is a
// 128-bit xorshift seeded from the world seed (NOT the same as Java's
// Random, but that is what the reference uses, and matching it keeps our
// output consistent with the generator we are porting).
#pragma once
#include <cstdint>

namespace cursestone::level::generator {

class GenRandom {
public:
    explicit GenRandom(int64_t seed) : seed_(seed) { setSeed(seed); }

    void setSeed(int64_t s);

    // NextInt() in [0, 2^31-1]; NextSignedInt() full 32-bit range shifted.
    int64_t nextInt();
    int64_t nextInt(int64_t bound);      // [0, bound)
    int64_t nextSignedInt();

    float  nextFloat();                  // [0, 1]
    float  nextSignedFloat();            // [-1, 1]
    bool   nextBoolean();
    int64_t nextLong();

    int64_t getSeed() const { return seed_; }

private:
    int64_t x_ = 0, y_ = 0, z_ = 0, w_ = 0;
    int64_t seed_ = 0;
};

} // namespace cursestone::level::generator
