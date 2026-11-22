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
// PerformanceManager — boot-time entry point of the performance module.
// Detects the CPU once, reports what the generator actually spends its
// time on (GenerationProfiler), and reports the SIMD dispatch decisions.
// It owns no threads and never blocks the tick loop.
#pragma once

namespace cursestone::performance {

class PerformanceManager {
  public:
    // Detect CPU features, pick the noise kernels (with the scalar
    // equivalence guard), and log one boot line. Idempotent.
    static void init();

    // Drain the profiler and log a per-chunk stage breakdown. Called at
    // meaningful points (spawn pregen finished) instead of on a timer,
    // so the numbers describe exactly the window that just ran.
    static void logStageBreakdown(const char* why);

    // Short description of the active noise kernel ("scalar", "avx2",
    // "neon", or "scalar (simd failed equivalence)").
    static const char* noiseKernelName();
};

} // namespace cursestone::performance
