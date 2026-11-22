/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// NoiseOptimizer — vector kernels for the vanilla Perlin octave regions,
// bit-exact against the scalar path.
//
// What is safe to vectorize, precisely:
//   * 2D regions (depthNoise): no state carries between points, every
//     point is an independent blend -> all points vectorize.
//   * 3D regions with sy >= 1 (the heavy minLimit/maxLimit octaves and
//     the first mainNoise octaves): consecutive y-steps always cross a
//     lattice cell, so vanilla's corner-reuse (yOld) never fires and
//     recomputing corners per y-step is exactly what the scalar loop
//     does. y-lanes are contiguous in the output buffer, so stores stay
//     vector.
//   * 3D regions with sy < 1: vanilla REUSES the corner blend from the
//     first y of a cell run (a stale fractional y) and carries yOld
//     across columns — order-sensitive, not expressible in y-lanes.
//     The dispatcher rejects those octaves and the scalar loop runs.
//
// The kernels use only exact IEEE ops in the scalar order — no FMA, no
// reassociation — so AVX2/NEON results match the scalar bytes. A
// one-time runtime guard compares vector vs scalar on fixed inputs and
// permanently disables the vector path on any mismatch, per process.
#pragma once

#include <cstdint>

namespace cursestone::performance::noise {

struct Params {
    const int* map;        // ImprovedNoise's 512-entry permutation table
    const float* mapf;     // float copy of the table, for gathers
    float ox, oy, oz;      // noise origin
    float px, py, pz;      // region world origin
    int xSize, ySize, zSize;
    float sx, sy, sz;      // per-octave scale (scale * pow)
    float pow;             // octave amplitude
    float* buffer;         // out: xSize*ySize*zSize floats
};

// Vector kernels. Return false when the call must fall back to scalar
// (vector path disabled, ISA mismatch, or 3D with sy < 1).
bool readArea2D(const Params& p);
bool readArea3D(const Params& p);

// One-time guard + kernel selection; returns the active kernel name.
const char* initialize();

// Registers the generator's own scalar loop as the guard reference.
// Called once before main() by ImprovedNoise; when set, the guard
// compares kernels against the platform's real scalar bits (whatever
// the compiler did to the scalar expressions) instead of the local
// mirror below. Returns true on first registration.
bool setScalarReference(void (*fn)(const Params&));

// Scalar reference implementation (mirrors ImprovedNoise::_readArea op
// for op). The runtime guard compares the vector kernels against it.
void region_scalar_2d(const Params& p);
void region_scalar_3d(const Params& p);

// Entry points provided by the ISA translation units; declared only for
// builds of the matching architecture (the TU is empty otherwise).
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
bool readArea2D_avx2(const Params& p);
bool readArea3D_avx2(const Params& p);
#endif
#if defined(__aarch64__)
bool readArea2D_neon(const Params& p);
bool readArea3D_neon(const Params& p);
#endif

} // namespace cursestone::performance::noise
