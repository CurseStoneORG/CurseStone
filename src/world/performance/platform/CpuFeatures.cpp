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
#include "world/performance/platform/CpuFeatures.h"

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#include <immintrin.h>
#endif

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#include <cpuid.h>
#endif

#if defined(__linux__) && defined(__aarch64__)
#include <sys/auxv.h>
#endif

#if defined(_WIN32) && defined(_M_ARM64)
#include <windows.h>
#endif

#include <thread>

namespace cursestone::performance {

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
static bool x86_gcc_detect(CpuFeatures& f) {
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
        return false;
    f.sse41 = (ecx >> 19) & 1;
    const bool osxsave = (ecx >> 27) & 1;
    f.fma3 = (ecx >> 12) & 1;

    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        f.bmi1 = (ebx >> 3) & 1;
        f.avx2 = (ebx >> 5) & 1;
        f.bmi2 = (ebx >> 8) & 1;
        f.avx512f = (ebx >> 16) & 1;
    }

    // SIMD state enabled by the OS: XMM/YMM bits of XCR0. AVX2 kernels
    // must not run if the OS does not save YMM on context switch.
    if (osxsave && (f.avx2 || f.fma3)) {
        unsigned int lo = 0, hi = 0;
        __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        const uint64_t xcr0 = (uint64_t)hi << 32 | lo;
        if ((xcr0 & 0x6) != 0x6) { // XMM + YMM state
            f.avx2 = false;
            f.fma3 = false;
            f.avx512f = false;
        }
    }
    return true;
}
#endif

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
static void x86_msvc_detect(CpuFeatures& f) {
    int regs[4] = {};
    __cpuid(regs, 1);
    f.sse41 = (regs[2] >> 19) & 1;
    const bool osxsave = (regs[2] >> 27) & 1;
    f.fma3 = (regs[2] >> 12) & 1;

    __cpuidex(regs, 7, 0);
    f.bmi1 = (regs[1] >> 3) & 1;
    f.avx2 = (regs[1] >> 5) & 1;
    f.bmi2 = (regs[1] >> 8) & 1;
    f.avx512f = (regs[1] >> 16) & 1;

    if (osxsave && (f.avx2 || f.fma3)) {
        const unsigned long long xcr0 = _xgetbv(0);
        if ((xcr0 & 0x6) != 0x6) {
            f.avx2 = false;
            f.fma3 = false;
            f.avx512f = false;
        }
    }
}
#endif

const CpuFeatures& CpuFeatures::get() {
    static const CpuFeatures features = [] {
        CpuFeatures f;
        f.logical_cores = (int)std::thread::hardware_concurrency();
        if (f.logical_cores <= 0)
            f.logical_cores = 1;

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
        x86_msvc_detect(f);
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
        x86_gcc_detect(f);
#elif defined(__aarch64__)
        f.neon = true; // base architecture feature
#if defined(__linux__)
        const unsigned long hwcap = getauxval(AT_HWCAP);
        f.neon_dotprod = (hwcap >> 20) & 1; // HWCAP_ASIMDDP
        f.crc32 = (hwcap >> 4) & 1;         // HWCAP_CRC32
#endif
#if defined(_WIN32)
        f.neon = IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE) != 0;
        f.neon_dotprod =
            IsProcessorFeaturePresent(PF_ARM_V82_DOTPROD_INSTRUCTIONS_AVAILABLE) != 0;
#endif
#endif
        return f;
    }();
    return features;
}

} // namespace cursestone::performance
