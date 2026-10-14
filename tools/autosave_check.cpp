// tools/autosave_check.cpp
//
// Test de regresion del autosave incremental — el arreglo del
// "[SAVE] Autosave took 759ms — this may cause disconnects!".
//
// Que se comprueba, con el codigo REAL (ChunkManager + Anvil + el
// generador Normal de verdad, o sea el mismo camino que usa
// World::save()/Server::tickAutosaveDrain):
//
//   1. ChunkManager::flushDirtyBudget(max_chunks, max_ms) NO escribe mas
//      de `max_chunks` chunks por llamada — que es lo que acota el coste
//      por tick del autosave.
//   2. Llamandolo en bucle se acaba vaciando la cola entera
//      (dirtyCount() == 0) y escribe exactamente lo que habia sucio.
//   3. flushDirty() (apagado / saveAll) sigue escribiendolo TODO.
//   4. Lo escrito con el nivel de compresion nuevo (kChunkCompressionLevel,
//      ver Anvil.cpp) se vuelve a LEER del .mca con los mismos bloques: el
//      nivel de zlib no es parte del formato, pero esto lo prueba sobre
//      bytes reales en vez de confiar en la teoria.
//
//   tools/run_autosave_check.sh
#include "world/ChunkManager.h"
#include "world/format/ChunkAdapter.h"
#include "world/format/anvil/Anvil.h"
#include "world/generator/Normal.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;

static void check(bool ok, const std::string& name) {
    printf("  [%s] %s\n", ok ? " OK " : "FAIL", name.c_str());
    if (!ok) ++failures;
}

int main() {
    const int SIDE = 3;                 // 9 chunks: se generan de verdad (lento)
    const size_t PER_CALL = 4;
    const std::string dir = "/tmp/autosave_check_world";
    if (system(("rm -rf " + dir).c_str()) != 0) { /* best effort */ }

    auto provider = std::make_shared<cursestone::world::format::anvil::Anvil>();
    provider->init(dir);
    provider->setSeed(1337);

    cursestone::world::ChunkManager cm;
    cm.setProvider(provider);
    cm.setGenerator(std::make_shared<cursestone::world::generator::Normal>(1337));

    // Generacion real: cada chunk generado queda dirty (asi es como se
    // llena la cola del autosave en un servidor en marcha).
    int generated = 0;
    for (int x = 0; x < SIDE; ++x)
        for (int z = 0; z < SIDE; ++z)
            if (cm.getChunk(x, z)) ++generated;

    printf("generated %d chunks, dirty=%zu\n", generated, cm.dirtyCount());
    check(generated == SIDE * SIDE, "todos los chunks se generaron");
    check(cm.dirtyCount() == (size_t)generated, "todos los generados estan sucios");

    // --- 1/2) flush con presupuesto ------------------------------------
    size_t wrote_first = (size_t)cm.flushDirtyBudget(PER_CALL, 0);
    check(wrote_first <= PER_CALL, "una llamada con presupuesto escribe <= max_chunks");
    check(wrote_first == PER_CALL, "con cola de sobra escribe exactamente max_chunks");
    check(cm.dirtyCount() == (size_t)generated - wrote_first,
          "dirtyCount() baja exactamente en lo escrito");

    size_t wrote = wrote_first;
    int calls = 0;
    bool every_call_bounded = true;
    while (cm.dirtyCount() > 0 && calls < 1000) {
        size_t n = (size_t)cm.flushDirtyBudget(PER_CALL, 0);
        if (n > PER_CALL) every_call_bounded = false;
        wrote += n;
        ++calls;
        if (n == 0 && cm.dirtyCount() > 0) break;   // sin progreso: se corta
    }
    printf("drenado en %d llamada(s) mas, %zu chunk(s) escritos\n", calls, wrote);
    check(every_call_bounded, "ninguna llamada del drenaje supera el presupuesto");
    check(cm.dirtyCount() == 0, "la cola se vacia drenando");
    check(wrote == (size_t)generated, "se escribio exactamente lo que habia sucio");

    // --- 4) y lo escrito se vuelve a leer ------------------------------
    // Comparo el bloque de la superficie del chunk (1,1) antes y despues
    // de ir a disco: si el nivel de compresion rompiera el formato, el
    // payload no decodificaria o los bytes no coincidirian.
    uint8_t top_before = 0, top_data_before = 0, id_below_before = 0;
    {
        mcp_chunk_t* c = cm.getChunkIfLoaded(1, 1);
        int32_t h = c ? mcp_chunk_get_height_map(c, 8, 8) : 0;
        top_before      = c ? mcp_chunk_get_block_id(c, 8, h, 8) : 0;
        top_data_before = c ? mcp_chunk_get_block_data(c, 8, h, 8) : 0;
        id_below_before = c ? mcp_chunk_get_block_id(c, 8, h - 1, 8) : 0;
    }

    // --- 3) flushDirty() completo --------------------------------------
    for (int x = 0; x < SIDE; ++x)
        for (int z = 0; z < SIDE; ++z)
            cm.markDirty(x, z);
    check(cm.dirtyCount() == (size_t)generated, "markDirty() vuelve a marcar los 9");
    int saved_all = cm.flushDirty();
    check(saved_all == generated, "flushDirty() escribe todos los sucios");
    check(cm.dirtyCount() == 0, "tras flushDirty() no queda nada sucio");

    bool same = false;
    {
        // The flush is now ASYNC: flushDirty() only hands encoded
        // snapshots to the background writer, so wait for the queue to
        // hit the disk before reading the .mca back — exactly what the
        // server's save-all / shutdown path does via World::save().
        provider->flushPendingSaves();
        auto back = provider->loadChunk(1, 1);
        if (back.has_value() && back.value()) {
            auto* adapter = dynamic_cast<cursestone::world::format::ChunkAdapter*>(
                back.value().get());
            mcp_chunk_t* c = adapter ? adapter->chunk() : nullptr;
            if (c) {
                int32_t h = mcp_chunk_get_height_map(c, 8, 8);
                same = mcp_chunk_get_block_id(c, 8, h, 8) == top_before &&
                       mcp_chunk_get_block_data(c, 8, h, 8) == top_data_before &&
                       mcp_chunk_get_block_id(c, 8, h - 1, 8) == id_below_before;
                printf("  releido: superficie id=%u data=%u, debajo id=%u (antes %u/%u/%u)\n",
                       mcp_chunk_get_block_id(c, 8, h, 8),
                       mcp_chunk_get_block_data(c, 8, h, 8),
                       mcp_chunk_get_block_id(c, 8, h - 1, 8),
                       top_before, top_data_before, id_below_before);
            }
        }
    }
    check(same, "el chunk escrito se relee del .mca con los mismos bloques");

    provider->shutdown();
    printf("\n%s (%d failure(s))\n",
           failures ? "AUTOSAVE CHECK: FAIL" : "AUTOSAVE CHECK: OK", failures);
    return failures ? 1 : 0;
}
