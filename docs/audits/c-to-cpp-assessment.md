# ¿Migrar el core C a C++? — Evaluación ejecutiva

Repo: CurseStone (branch `stable`, HEAD `013dc90`). Alcance: todo el código C propio
(`src/`), excluyendo `third_party/lua` (vendored, no se toca jamás).

---

## 1. Veredicto ejecutivo

**No es necesario convertir todo el C a C++. Sí es recomendable una migración
selectiva por fases que cubre ~40% del C y elimina el 80% del riesgo real.**

El C propio son **10.748 LOC en 74 archivos** frente a **54.857 LOC de C++ en 433
archivos** (17% del código). La capa C++ ya es moderna (factorías `make_*` en casi
todo el árbol, ~8 `new` crudos en total según la auditoría de memoria). El C no es
un lastre generalizado: es un núcleo pequeño y ya endurecido. Un big-bang C→C++
tocaría el protocolo de red, el formato de disco y los hot paths de chunks para
obtener cero beneficio de rendimiento y riesgo alto de regresión contra un cliente
(0.15.10, protocolo 84) que no perdona cambios de comportamiento en el wire.

Lo que sí merece la pena: convertir NBT y los módulos hoja de `utils`/`math`
(trivial), blindar las fronteras con RAII sin tocar el C (gratis), y dejar raklib y
Chunk/Anvil en C con encapsulación — son precisamente los módulos donde una
conversión a clases aportaría menos y rompería más.

---

## 2. Estado actual del C propio

| Módulo | Archivos | LOC | Llamadas malloc-family | Rol |
|---|---|---|---|---|
| `network/raklib` (+`protocol/`) | 39 | 5.295 | 157 | UDP, sesiones, paquetes RakNet |
| `nbt` (+`tag/`) | 14 | 1.521 | 129 | NBT binario (worlds, items, login) |
| `world/format` (Chunk/ChunkSection/Anvil) | 3 | 1.892 | 73 | Chunk en memoria + codec Anvil |
| `utils` (Binary, Logger, ThreadPort, Random, UUID…) | 11 | 1.274 | 12 | Serialización y primitivas |
| `logger` (AsyncMainLogger) | 1 | 457 | 0 | Logger asíncrono con worker |
| `math` (Vector2/3, AABB, Math) | 6 | 309 | 0 | POD puro, `extern "C"` |
| **Total propio** | **74** | **10.748** | **~371** | |

Datos de acoplamiento:

- **71 archivos .cpp** incluyen cabeceras C (`world/format` 30, `utils` 28, `nbt` 16,
  `math` 12, `logger` 1, con solapamiento).
- raklib está aislado detrás de **un único puente**: `RakLibInterface.cpp` ↔
  `raklib/ServerHandler.h`. Ningún otro .cpp toca raklib.
- El patrón de memoria está documentado en el propio `Chunk.h`: el `Chunk` es un
  struct C plano que posee sus 16 secciones (`mcp_chunk_new/mcp_chunk_destroy`), y
  la interfaz C++ `FullChunk` lo envuelve. Es un contrato deliberado, no legacy
  accidental: el layout plano es lo que el codec Anvil escribe a disco y lo que el
  generador escribió a 13.6 ms/chunk.

---

## 3. Riesgos con evidencia (auditoría de memoria, commit `2afbc53`)

La auditoría profunda de memoria (30 defectos corregidos, todo verificado con
ASan/UBSan/LSan) sirve como radiografía exacta de dónde vive el riesgo:

- **C1** — leak de sesión en el kick path, `raklib/ServerHandler.c`: rojo 1.490 B /
  21 allocs por sesión kickeada, verde 0 tras el fix. Clásico error path en C.
- **C2** — doble leak en pong/MOTD (raklib). **C3** — leak de buffer de
  serialización ×2 en el camino chunk→bytes.
- **A1** — `split_count` divergente entre decode y cleanup → acceso OOB (raklib).
- **A2** — double-realloc con puntero colgante en la cola asíncrona.
- **Serie R** — 10 asignaciones sin cota en paths de decode C: NBT depth, inflate
  de Batch/Login, PlayerList reserve, player-dat, buffers Anvil.
- La serie U (UB aritmético) y la mayoría de UAF vivían en C++, no en C.

**Lectura correcta de esta evidencia:** el riesgo no era "C" en abstracto, sino
*ownership manual + error paths + decode de input hostil*, y ese código está
exactamente en raklib y NBT. Tras el audit ese riesgo está mitigado con fixes
probados red/green; lo que la migración compraría es que esa clase de bug *no
pueda escribirse* (destructores, `unique_ptr`, límites de tipo), no arreglar los
que ya están arreglados.

---

## 4. Comparativa esfuerzo vs beneficio

| Módulo | LOC | Esfuerzo | Riesgo de conversión | Beneficio real | Veredicto |
|---|---|---|---|---|---|
| `math` | 309 | Trivial (horas) | ~0 | Estética; ya es POD `extern "C"` | Convertir cuando toque |
| `utils` hojas (Random, UUID, Binary, TextFormat, Color) | ~600 | Bajo | Bajo | Moderado (sin leaks reales ahí) | Fase 2 |
| `nbt` (+tag) | 1.521 | Medio (~2-3 días) | Medio: 16 .cpp consumidores, decode hostil | **Alto**: elimina 129 malloc/free y la clase entera de bugs R-series | **Convertir (fase 2) — el mejor ratio** |
| `logger` + `ThreadPort` | ~700 | Medio | Medio: hilos y shutdown ordering (L1/L2 ya frágiles) | Bajo: 0 memcalls, ya funciona | Dejar en C |
| `world/format` (Chunk/Anvil) | 1.892 | Alto | **Alto**: formato de disco + hot path + layout que el generador y Anvil comparten | Bajo: 73 memcalls ya acotadas y auditadas | **No convertir**; encapsular con RAII en C++ |
| `raklib` (+protocol) | 5.295 | **Alto (~2-3 semanas)** | **Muy alto**: wire format, timing, session state, un solo puente | Medio: 157 memcalls, pero C1/A1 ya fijados | **No convertir ahora**; re-evaluar solo si reaparecen bugs de sesión |

El 40% convertido (math + utils hojas + NBT ≈ 2.400 LOC) elimina los dos mayores
concentradores de memoria manual tras raklib (NBT) a cambio del esfuerzo más bajo
de toda la tabla.

---

## 5. Ruta de migración recomendada

- **Fase 0 — fronteras RAII, sin tocar C (medio día, riesgo nulo).** Deleters
  `unique_ptr` para `mcp_chunk_destroy`, NBT y buffers raklib en los 71 .cpp que
  consumen C. Convierte cada `free` olvidable en imposible, incluso si el C queda
  como está. Es la mayor compra por unidad de esfuerzo de todo el plan.
- **Fase 1 — `math` + hojas de `utils`** (1 día). Conversión mecánica a tipos C++
  inline; los `.hpp` ya existen a medias (`Vector3.hpp`).
- **Fase 2 — NBT a C++ RAII** (2-3 días). Clase con destructor + `std::vector`
  para payloads; el decode hostil pasa a estar acotado por tipos. Los 16
  consumidores se migran de `mcp_nbt_*` a métodos con el mismo nombre.
- **Fase 3 — opcional y condicionada: raklib.** Solo si reaparecen bugs de sesión
  o al encarar un feature grande de red. Hacerla "porque sí" es el escenario de
  peor ratio de todo el informe.
- **Nunca:** `third_party/lua`; el layout binario de Chunk/Anvil mientras el
  formato de mundo no cambie.

---

## 6. Lo que la migración NO compra

- **Rendimiento:** gcc-14 genera código equivalente para este C (ya medido: el
  costo de generación está en el ruido de Perlin y la E/S, no en el ABI C). No hay
  p50/p99 que ganar.
- **Menos bugs hoy:** los 30 defectos del audit ya están corregidos y con tests
  (`memory_audit_checks.py`, 7 escenarios) en CI local.
- **Simplicidad de build:** el build ya compila C y C++ juntos en un solo target;
  migrar no simplifica CMake ni el CI.

---

## 7. Conclusión

¿Es *necesario*? **No.** El sistema está sano, auditado y el C es solo el 17% del
código tras una frontera limpia. ¿Es *recomendable*? **Parcialmente y por fases**:
Fase 0 (RAII en fronteras) + Fase 2 (NBT) dan ~80% del beneficio con ~5% del
riesgo de un rewrite total; raklib y Chunk/Anvil rinden más como C bien
encapsulado que como C++ rehecho. La energía de ingeniería rinde más aplicada a
features y al bug pendiente del inventario creativo que a una reescritura
cosmética.
