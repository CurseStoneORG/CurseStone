#!/usr/bin/env bash
# Build local sin CMake (el sandbox no lo trae). Mismo set de fuentes y
# flags que CMakeLists.txt: C23 para los .c, C++20 para los .cpp, -Isrc,
# y link -lpthread -lz -ldl -rdynamic.
set -euo pipefail
cd "$(dirname "$0")"

OUT="${OUT:-/tmp/cs_build}"
mkdir -p "$OUT/obj"

CFLAGS="-std=c2x -O2 -w -Isrc"
CXXFLAGS="-std=c++20 -O2 -w -Isrc -pthread"

rm -f "$OUT/.fail"
pids=()
n=0
# Un objeto solo se reutiliza si es MAS NUEVO que su .cpp Y que TODOS los
# headers del proyecto: sin esto, editar un .h dejaba el resto de los .o
# con el layout viejo de la clase (ODR roto) y los fallos aparecian como
# comportamiento raro en vez de como error de compilacion.
NEWEST_HDR=$(find src -name '*.h' -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2-)
while IFS= read -r f; do
  o="$OUT/obj/${f//\//_}.o"
  if [ -f "$o" ] && [ "$o" -nt "$f" ] && [ "$o" -nt "$NEWEST_HDR" ]; then continue; fi
  # El .o viejo se borra ANTES de compilar: si la compilacion falla, no
  # puede quedar un objeto obsoleto que haga pasar el link como si todo
  # estuviera bien (eso ya nos oculto un error una vez).
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
  echo "BUILD FAILED: hubo errores de compilacion (ver arriba)" >&2
  exit 1
fi
echo "built $OUT/CurseStone ($(ls -la "$OUT/CurseStone" | awk '{print $5}') bytes)"