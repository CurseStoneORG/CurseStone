#!/usr/bin/env bash
# Local build without CMake (the sandbox does not ship it). Same sources and
# flags as CMakeLists.txt: C23 for .c, C++20 for .cpp, -Isrc,
# and link with -lpthread -lz -ldl -rdynamic.
set -euo pipefail
cd "$(dirname "$0")"

OUT="${OUT:-/tmp/cs_build}"
mkdir -p "$OUT/obj"

CFLAGS="-std=c2x -O2 -w -Isrc"
CXXFLAGS="-std=c++20 -O2 -w -Isrc -pthread"

rm -f "$OUT/.fail"
pids=()
n=0
# An object is reused only if it is NEWER than its source file AND than ALL
# project headers. Without this, editing a .h left the other .o files with the
# old class layout (broken ODR) and failures showed up as odd runtime behavior
# instead of compile errors.
NEWEST_HDR=$(find src -name '*.h' -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2-)
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

g++ -rdynamic -pthread -o "$OUT/CurseStone" "$OUT"/obj/*.o -lz -ldl
if [ -f "$OUT/.fail" ]; then
  echo "BUILD FAILED: compilation errors occurred (see above)" >&2
  exit 1
fi
echo "built $OUT/CurseStone ($(ls -la "$OUT/CurseStone" | awk '{print $5}') bytes)"