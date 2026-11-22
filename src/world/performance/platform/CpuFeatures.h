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
// Runtime CPU feature detection. Read once at boot, cached forever.
// Nothing here is required for the server to run — the hot paths keep a
// plain scalar fallback, so a CPU with no SIMD features still works.
#pragma once

namespace cursestone::performance {

struct CpuFeatures {
    // x86
    bool sse41 = false;
    bool avx2 = false;
    bool avx512f = false;
    bool fma3 = false;
    bool bmi1 = false;
    bool bmi2 = false;
    // ARM64: ASIMD is part of the base architecture, so it is true on
    // every aarch64 build; dot-product and crypto are optional extensions.
    bool neon = false;
    bool neon_dotprod = false;
    bool crc32 = false;

    int logical_cores = 0;
    int cache_line = 64;

    // Computed once on first use.
    static const CpuFeatures& get();
};

} // namespace cursestone::performance
