#!/usr/bin/env bash
# Local build without CMake (the sandbox does not ship it). Same sources and
# flags as CMakeLists.txt: C23 for .c, C++20 for .cpp, -Isrc,
# -Ithird_party/lua (bundled Lua 5.4 headers), -g debug symbols in every
# build type (crash backtraces symbolize to source lines), and link with
# -lpthread -lz -ldl -rdynamic.
set -euo pipefail
cd "$(dirname "$0")"

OUT="${OUT:-/tmp/cs_build}"
mkdir -p "$OUT/obj"

CFLAGS="-std=c2x -O2 -g -w -Isrc -Ithird_party/lua"
CXXFLAGS="-std=c++20 -O2 -g -w -Isrc -Ithird_party/lua -pthread"

rm -f "$OUT/.fail"
pids=()
n=0
# An object is reused only if it is NEWER than its source file AND than ALL
# project headers. Without this, editing a .h left the other .o files with the
# old class layout (broken ODR) and failures showed up as odd runtime behavior
# instead of compile errors.
#
# NOTE: this must NOT be `sort -rn | head -1` — head exits after the first
# line and sort can be SIGPIPE'd mid-write under load, which with pipefail
# aborts the whole build with exit 141 and no diagnostics. awk below reads
# the full stream, so no producer can die of SIGPIPE.
NEWEST_HDR=$(find src -name '*.h' -printf '%T@ %p\n' \
  | awk 'BEGIN{m=-1} {if ($1+0 > m) {m=$1+0; p=$0}} END{if (p!="") {sub(/^[^ ]* /,"",p); print p}}')
while IFS= read -r f; do
  o="$OUT/obj/${f//\//_}.o"
  if [ -f "$o" ] && [ "$o" -nt "$f" ] && [ "$o" -nt "$NEWEST_HDR" ]; then continue; fi
  # Delete the old .o BEFORE compiling: if compilation fails, no stale object
  # can remain and let the link pass as if everything was fine (this hid an
  # error once).
  rm -f "$o"
  ( gcc $CFLAGS -c "$f" -o "$o" || touch "$OUT/.fail" ) &
  pids+=($!); n=$((n+1))
  if [ $((n % 4)) -eq 0 ]; then wait; fi
done < <(find src -name '*.c')
wait

n=0
while IFS= read -r f; do
  o="$OUT/obj/${f//\//_}.o"
  if [ -f "$o" ] && [ "$o" -nt "$f" ] && [ "$o" -nt "$NEWEST_HDR" ]; then continue; fi
  rm -f "$o"
  ( g++ $CXXFLAGS -c "$f" -o "$o" || touch "$OUT/.fail" ) &
  pids+=($!); n=$((n+1))
  if [ $((n % 4)) -eq 0 ]; then wait; fi
done < <(find src -name '*.cpp')
wait

# Bundled Lua 5.4 — mirrors CMakeLists.txt (static lib linked into the exe;
# lua.c/luac.c excluded because they define main()). Without this the link
# fails with undefined references to lua_*.
n=0
while IFS= read -r f; do
  o="$OUT/obj/${f//\//_}.o"
  if [ -f "$o" ] && [ "$o" -nt "$f" ]; then continue; fi
  rm -f "$o"
  ( gcc $CFLAGS -c "$f" -o "$o" || touch "$OUT/.fail" ) &
  pids+=($!); n=$((n+1))
  if [ $((n % 4)) -eq 0 ]; then wait; fi
done < <(find third_party/lua -maxdepth 1 -name '*.c' ! -name 'lua.c' ! -name 'luac.c')
wait

g++ -rdynamic -pthread -o "$OUT/CurseStone" "$OUT"/obj/*.o -lz -ldl
if [ -f "$OUT/.fail" ]; then
  echo "BUILD FAILED: compilation errors occurred (see above)" >&2
  exit 1
fi
echo "built $OUT/CurseStone ($(ls -la "$OUT/CurseStone" | awk '{print $5}') bytes)"