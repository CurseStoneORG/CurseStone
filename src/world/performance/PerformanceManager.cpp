/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
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
#include "PerformanceManager.h"

#include "GenerationProfiler.h"
#include "platform/CpuFeatures.h"
#include "utils/MainLogger.hpp"

#include <cstdio>
#include <string>

namespace cursestone::performance {

namespace {
const char* kStageNames[] = {
    "total",  "terrain", "noise",   "surface", "caves", "biome",
    "populate", "copy_in", "copy_out", "light", "encode", "snapshot",
    "disk",
};
static_assert(sizeof(kStageNames) / sizeof(kStageNames[0]) ==
                  (size_t)Stage::Count,
              "stage name table out of sync");
} // namespace

void PerformanceManager::init() {
    const CpuFeatures& cpu = CpuFeatures::get();
    char line[192];
    std::snprintf(line, sizeof(line),
                  "[Perf] CPU: %d cores | noise kernel: %s | "
                  "avx2=%d avx512f=%d fma=%d sse4.1=%d neon=%d",
                  cpu.logical_cores, noiseKernelName(), cpu.avx2 ? 1 : 0,
                  cpu.avx512f ? 1 : 0, cpu.fma3 ? 1 : 0, cpu.sse41 ? 1 : 0,
                  cpu.neon ? 1 : 0);
    utils::MainLogger::get().info(line);
}

void PerformanceManager::logStageBreakdown(const char* why) {
    const GenerationProfiler::Snapshot snap = GenerationProfiler::drain();
    const uint64_t totalChunks = snap.stages[(size_t)Stage::Total].calls;
    if (totalChunks == 0)
        return;

    // One compact line per half of the pipeline keeps the log readable
    // while still showing every measured stage.
    char gen[256];
    int off = std::snprintf(gen, sizeof(gen), "[Perf] %s: %llu chunks",
                            why, (unsigned long long)totalChunks);
    for (size_t i = (size_t)Stage::Total; i <= (size_t)Stage::Populate && off > 0;
         ++i) {
        const auto& e = snap.stages[i];
        if (e.calls == 0)
            continue;
        const double us = (double)e.ns / (double)e.calls / 1000.0;
        off += std::snprintf(gen + off, sizeof(gen) - off, " | %s %.0fus/%llu",
                             kStageNames[i], us, (unsigned long long)e.calls);
    }
    utils::MainLogger::get().info(gen);

    char io[192];
    off = std::snprintf(io, sizeof(io), "[Perf] %s io:", why);
    for (size_t i = (size_t)Stage::CopyIn; i < (size_t)Stage::Count && off > 0;
         ++i) {
        const auto& e = snap.stages[i];
        if (e.calls == 0)
            continue;
        const double us = (double)e.ns / (double)e.calls / 1000.0;
        off += std::snprintf(io + off, sizeof(io) - off, " %s %.0fus/%llu",
                             kStageNames[i], us, (unsigned long long)e.calls);
    }
    utils::MainLogger::get().info(io);
}

const char* PerformanceManager::noiseKernelName() {
    // Wired by NoiseOptimizer when the SIMD kernels land; the default
    // stands until then.
    static const char* name = "scalar";
    return name;
}

} // namespace cursestone::performance
