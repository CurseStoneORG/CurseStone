#!/usr/bin/env bash
# check_portability.sh — fails if a Linux ELF binary requires glibc symbol
# versions newer than a ceiling, or still pulls libstdc++/libgcc symbols
# from the host. CI runs it right after the Linux x86_64 build so a runner
# or toolchain drift (e.g. ubuntu-latest shipping glibc 2.43) can never
# again ship a binary that older hosts refuse with "GLIBC_2.xx not found".
#
#   tools/check_portability.sh <binary> [ceiling]
#
# The ceiling is the glibc of the oldest supported host, as GLIBC_X.Y
# (default 2.31 — Ubuntu 20.04, the base of the Temurin focal images most
# hosting panels run). The libstdc++/libgcc check expects them statically
# linked: any GLIBCXX/CXXABI/GLIBC_PRIVATE dynamic reference means the
# host would need a matching C++ runtime it may not actually have.
#
#   tools/check_portability.sh build/linux-release/CurseStone 2.31
set -euo pipefail

BIN="${1:?usage: check_portability.sh <binary> [ceiling]}"
CEILING="${2:-2.31}"

if [ ! -f "$BIN" ]; then
    echo "check_portability: '$BIN' is not a file" >&2
    exit 1
fi

if ! command -v objdump >/dev/null 2>&1; then
    echo "check_portability: objdump not found (install binutils)" >&2
    exit 1
fi

# Newest glibc symbol version referenced by the dynamic symbols.
MAX_GLIBC=$(objdump -T "$BIN" | grep -oE 'GLIBC_2\.[0-9]+' | sort -Vu | tail -1 || true)

# Any C++ runtime or glibc-private requirement left in the binary means a
# host dependency the ceiling cannot express (libstdc++ version skew, or
# symbols bound to one exact glibc build).
CXX_REFS=$(objdump -T "$BIN" | grep -E 'GLIBCXX|CXXABI|GLIBC_PRIVATE' || true)

echo "check_portability: $BIN"
echo "  glibc ceiling:     GLIBC_${CEILING}"
echo "  glibc requirement: ${MAX_GLIBC:-none}"

if [ -n "$CXX_REFS" ]; then
    echo "  FAIL: dynamic libstdc++/libgcc/glibc-private requirements found —" >&2
    echo "        the C++ runtime must be statically linked (-static-libstdc++ -static-libgcc)." >&2
    echo "$CXX_REFS" | head -5 >&2
    exit 1
fi

if [ -n "$MAX_GLIBC" ]; then
    NEWEST=$(printf '%s\n%s\n' "GLIBC_${CEILING}" "$MAX_GLIBC" | sort -V | tail -1)
    if [ "$NEWEST" = "$MAX_GLIBC" ] && [ "$MAX_GLIBC" != "GLIBC_${CEILING}" ]; then
        echo "  FAIL: requires $MAX_GLIBC, newer than the ceiling GLIBC_${CEILING}." >&2
        echo "        The binary will not start on older hosts. Build against an" >&2
        echo "        older glibc sysroot (see the build-linux container pin)." >&2
        exit 1
    fi
fi

echo "  OK: portable — runs on any host with glibc >= ${CEILING}"
