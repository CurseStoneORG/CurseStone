// SPDX-License-Identifier: LGPL-3.0-or-later
#include "world/generator/noise/GenRandom.h"

namespace cursestone::world::generator {

void GenRandom::setSeed(int64_t s) {
    seed_ = s;
    const int64_t X = 123456789;
    const int64_t Y = 362436069;
    const int64_t Z = 521288629;
    const int64_t W = 88675123;
    x_ = X ^ s;
    // NOTE: the parentheses around the `|` are REQUIRED and were missing
    // before. `|` binds looser than `^`, so `Y ^ a | b & c` parsed as
    // `(Y ^ a) | (b & c)` instead of the intended `Y ^ (a | (b & c))` —
    // which both mixed a signed seed into the low bits and, worse, made
    // the generator state depend on operator precedence rather than on
    // the formula. Any platform where int64_t shifts behaved differently
    // produced a different sequence, so worlds stopped being reproducible.
    y_ = Y ^ ((s << 17) | (((s >> 15) & 0x7fffffff) & 0xffffffff));
    z_ = Z ^ ((s << 31) | (((s >> 1) & 0x7fffffff) & 0xffffffff));
    w_ = W ^ ((s << 18) | (((s >> 14) & 0x7fffffff) & 0xffffffff));
}

int64_t GenRandom::nextSignedInt() {
    int64_t t = (x_ ^ (x_ << 11)) & 0xffffffff;
    x_ = y_;
    y_ = z_;
    z_ = w_;
    w_ = (w_ ^ ((w_ >> 19) & 0x7fffffff) ^ (t ^ ((t >> 8) & 0x7fffffff))) & 0xffffffff;
    return w_;
}

int64_t GenRandom::nextInt() {
    return nextSignedInt() & 0x7fffffff;
}

int64_t GenRandom::nextInt(int64_t bound) {
    if (bound <= 0) return 0;
    return nextInt() % bound;
}

float GenRandom::nextFloat() {
    return (float)nextInt() / 2147483647.f;
}

float GenRandom::nextSignedFloat() {
    return (float)nextSignedInt() / 2147483647.f;
}

bool GenRandom::nextBoolean() {
    return (nextSignedInt() & 0x01) == 0;
}

int64_t GenRandom::nextLong() {
    return (nextSignedInt() << 32) | nextSignedInt();
}

} // namespace cursestone::world::generator
