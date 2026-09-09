#include "RedstoneManager.h"
#include "block/BlockIds.h"
#include "level/Level.h"
#include "Server.h"
#include "Player.h"
#include "network/protocol/UpdateBlockPacket.h"
#include <algorithm>

namespace atomikpe::redstone {

using atomikpe::level::Level;

// ============================================================
// broadcastBlock — avisa a todos los jugadores que estén viendo el
// chunk de (x,y,z) que el bloque ahí cambió, para que lo vean SIN
// tener que reconectar. Mismo patrón exacto que ya usa Player.cpp al
// romper/colocar bloques (mismo paquete, mismo criterio de a quién
// mandárselo).
// ============================================================
static void broadcastBlock(atomikpe::Server* server, int32_t x, int32_t y, int32_t z,
                             uint8_t blockId, uint8_t blockData) {
    if (!server) return;
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = x;
    ubp->z = z;
    ubp->y = (uint8_t)y;
    ubp->blockId = blockId;
    ubp->blockData = blockData;
    ubp->flags = 0xFF;

    int32_t cx = x >> 4;
    int32_t cz = z >> 4;
    for (auto& player : server->playersSnapshot()) {
        if (!player) continue;
        if (!player->isTrackingChunk(cx, cz)) continue;
        player->sendPacket(ubp);
    }
}

// ============================================================
// getEmittedPower — cuánta señal aporta este bloque hacia afuera.
// ============================================================
int getEmittedPower(Level* level, int32_t x, int32_t y, int32_t z) {
    if (!level) return 0;
    if (y < 0 || y >= 256) return 0;
    // CRÍTICO: no leer bloques en chunks no cargados. Level::getBlockId
    // puede disparar generación síncrona de chunk si el chunk no está
    // en memoria — exactamente el patrón que causó el freeze de 88s en
    // BlockFire v5 (propagación tocando vecinos sin este chequeo). Un
    // vecino en un chunk no cargado se trata simplemente como "sin
    // señal", nunca se fuerza su carga.
    if (!level->isChunkLoaded(x, z)) return 0;
    uint8_t id = level->getBlockId(x, y, z);
    uint8_t data = level->getBlockData(x, y, z);

    switch (id) {
        case block::REDSTONE_TORCH_ON:
            // Antorcha encendida: potencia máxima hacia los vecinos
            // laterales y hacia arriba (no hacia el bloque que la
            // sostiene — comportamiento real de Minecraft).
            return 15;
        case block::REDSTONE_BLOCK:
            return 15;
        case block::LEVER:
            // Bit 0x8 = encendida (convención PMMP/vanilla para lever).
            return (data & 0x08) ? 15 : 0;
        case block::STONE_BUTTON:
            return (data & 0x08) ? 15 : 0;
        case block::WOOD_PRESSURE_PLATE:
        case block::STONE_PRESSURE_PLATE:
            // data almacena 0 (sin pisar) o 1 (pisada) para placas simples.
            return (data != 0) ? 15 : 0;
        case block::REDSTONE_WIRE:
            // El wire emite su propia potencia actual (almacenada en data).
            return data & 0x0F;
        default:
            return 0;
    }
}

// ============================================================
// isPowered — ¿este bloque está recibiendo señal de algún vecino?
// (No es lo mismo que getEmittedPower: una palanca apagada no emite
// nada, pero un bloque JUNTO a una palanca encendida sí está powered.)
// ============================================================
bool isPowered(Level* level, int32_t x, int32_t y, int32_t z) {
    if (!level) return false;
    // Los 6 vecinos directos.
    static const int32_t dx[6] = {1, -1, 0, 0, 0, 0};
    static const int32_t dy[6] = {0, 0, 1, -1, 0, 0};
    static const int32_t dz[6] = {0, 0, 0, 0, 1, -1};
    for (int i = 0; i < 6; ++i) {
        int32_t nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
        if (ny < 0 || ny >= 256) continue;
        if (!level->isChunkLoaded(nx, nz)) continue;
        if (getEmittedPower(level, nx, ny, nz) > 0) return true;
    }
    // El propio bloque, si es wire con potencia > 0, cuenta como
    // "powered" para sí mismo (lo consultan cosas colocadas sobre él).
    if (!level->isChunkLoaded(x, z)) return false;
    uint8_t id = level->getBlockId(x, y, z);
    if (id == block::REDSTONE_WIRE) {
        return (level->getBlockData(x, y, z) & 0x0F) > 0;
    }
    return false;
}

// ============================================================
// tryInteract — palanca y botón. Convención vanilla: bits 0-2 (o 0-5
// según variante) guardan la orientación/cara de colocación, y el bit
// 0x08 guarda el estado encendido (1) / apagado (0). Alternar el
// estado preserva los demás bits intactos, así la orientación visual
// del bloque no cambia al accionarlo.
// ============================================================
bool tryInteract(Level* level, atomikpe::Server* server, int32_t x, int32_t y, int32_t z) {
    if (!level) return false;
    if (y < 0 || y >= 256) return false;
    if (!level->isChunkLoaded(x, z)) return false;
    uint8_t id = level->getBlockId(x, y, z);
    uint8_t data = level->getBlockData(x, y, z);

    if (id == block::LEVER || id == block::STONE_BUTTON) {
        // El botón "ideal" se auto-apaga solo tras un tiempo (vanilla
        // real); eso requiere el scheduler, que se deja como extensión
        // futura documentada — por ahora ambos funcionan como toggle
        // manual, simple y sin riesgo de tareas recurrentes.
        uint8_t newData = data ^ 0x08;
        level->setBlock(x, y, z, id, newData);
        broadcastBlock(server, x, y, z, id, newData); // FIX: se veía solo al reconectar
        notifyNeighbors(level, server, x, y, z);
        return true;
    }

    if (id == block::WOODEN_DOOR) {
        // La puerta de madera (a diferencia de la de hierro) se abre
        // con clic directo del jugador en vanilla real, además de con
        // redstone. La de hierro (IRON_DOOR) NO reacciona a clic —
        // sólo a redstone — así que se excluye aquí a propósito.
        //
        // Igual que en updateDoorAt: sólo la mitad INFERIOR guarda el
        // bit 0x04 de abierta (ver esquema de bits documentado en
        // updateDoorAt más abajo). Si el jugador clickea la mitad
        // superior, hay que localizar la inferior para saber cuál
        // bit alternar — el cliente ya envía clicks contra cualquiera
        // de las dos mitades indistintamente.
        bool isTopHalf = (data & 0x08) != 0;
        int32_t lowerY = isTopHalf ? (y - 1) : y;
        if (lowerY < 0 || lowerY >= 256) return false;
        if (!level->isChunkLoaded(x, z)) return false;
        uint8_t lowerId = level->getBlockId(x, lowerY, z);
        if (lowerId != block::WOODEN_DOOR) return false; // desincronizada — no tocar
        uint8_t lowerData = level->getBlockData(x, lowerY, z);
        if ((lowerData & 0x08) != 0) return false; // salvaguarda de datos corruptos, igual que updateDoorAt

        uint8_t newLowerData = lowerData ^ 0x04;
        level->setBlock(x, lowerY, z, block::WOODEN_DOOR, newLowerData);
        broadcastBlock(server, x, lowerY, z, block::WOODEN_DOOR, newLowerData);
        // No se llama a notifyNeighbors aquí: abrir/cerrar la puerta a
        // mano no debería re-disparar su propia reevaluación por
        // redstone en el mismo instante (causaría que, si hay señal
        // activa, el clic manual se revierta inmediatamente sin que el
        // jugador perciba haber logrado nada). El siguiente evento de
        // redstone real en la zona sí la recalculará con normalidad.
        return true;
    }
    return false;
}

// ============================================================
// updateWireAt — recalcula la potencia de UN wire y, si cambió,
// propaga a los wires vecinos. Acotado: cada llamada recalcula un
// único bloque; la "cascada" ocurre por llamadas recursivas directas,
// nunca por tareas re-encoladas, y el valor de potencia solo puede
// bajar 1 por paso desde una fuente — con 15 niveles posibles, la
// cadena de llamadas recursivas está matemáticamente acotada a un
// máximo de 15 bloques de wire consecutivos antes de que la potencia
// llegue a 0 y la recursión se detenga por sí sola.
// ============================================================
static void updateWireInternal(Level* level, atomikpe::Server* server,
                                 int32_t x, int32_t y, int32_t z, int depth) {
    if (!level) return;
    if (depth > 16) return; // guarda de seguridad extra (nunca debería alcanzarse)
    if (y < 0 || y >= 256) return;
    if (!level->isChunkLoaded(x, z)) return;
    uint8_t id = level->getBlockId(x, y, z);
    if (id != block::REDSTONE_WIRE) return;

    uint8_t oldData = level->getBlockData(x, y, z);
    int oldPower = oldData & 0x0F;

    // La potencia de este wire es el máximo entre:
    //  - la señal directa más fuerte de un vecino no-wire (antorcha,
    //    bloque de redstone, palanca, etc.), sin decaer
    //  - la potencia del wire vecino más fuerte, menos 1 (decaimiento)
    int best = 0;
    static const int32_t dx[6] = {1, -1, 0, 0, 0, 0};
    static const int32_t dy[6] = {0, 0, 1, -1, 0, 0};
    static const int32_t dz[6] = {0, 0, 0, 0, 1, -1};
    for (int i = 0; i < 6; ++i) {
        int32_t nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
        if (ny < 0 || ny >= 256) continue;
        if (!level->isChunkLoaded(nx, nz)) continue;
        uint8_t nid = level->getBlockId(nx, ny, nz);
        if (nid == block::REDSTONE_WIRE) {
            int npower = level->getBlockData(nx, ny, nz) & 0x0F;
            best = std::max(best, npower - 1);
        } else {
            int emitted = getEmittedPower(level, nx, ny, nz);
            best = std::max(best, emitted);
        }
    }
    best = std::max(0, std::min(15, best));

    if (best == oldPower) return; // sin cambio, no propagar más

    level->setBlock(x, y, z, block::REDSTONE_WIRE, (uint8_t)best);
    // FIX (reportado por el usuario): sin esto, el cambio de potencia
    // del wire solo se veía al reconectar, porque nunca se avisaba a
    // los jugadores ya conectados de que este bloque específico cambió.
    broadcastBlock(server, x, y, z, block::REDSTONE_WIRE, (uint8_t)best);

    // Cualquier vecino no-wire que dependa de recibir señal DESDE este
    // wire (lámparas, antorchas de redstone vía su soporte, puertas,
    // pistones) también debe re-evaluarse, porque su potencia
    // disponible acaba de cambiar. IMPORTANTE: esta lista debe
    // mantenerse en sincronía con la de notifyNeighbors() más abajo —
    // un wire es la ÚNICA vía por la que un cambio de señal puede
    // llegar a un vecino sin pasar por notifyNeighbors del bloque
    // origen (porque el wire re-propaga en cascada por sí mismo, no
    // sólo hacia sus 6 vecinos inmediatos del bloque que cambió
    // primero). Sin este bloque, un circuito palanca→wire→pistón (con
    // al menos 1 bloque de wire de por medio) nunca activaría el
    // pistón, porque el pistón no es vecino directo de la palanca.
    for (int i = 0; i < 6; ++i) {
        int32_t nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
        if (ny < 0 || ny >= 256) continue;
        if (!level->isChunkLoaded(nx, nz)) continue;
        uint8_t nid = level->getBlockId(nx, ny, nz);
        if (nid == block::REDSTONE_WIRE) {
            updateWireInternal(level, server, nx, ny, nz, depth + 1);
        } else if (nid == block::REDSTONE_LAMP || nid == block::LIT_REDSTONE_LAMP) {
            updateLampAt(level, server, nx, ny, nz);
        } else if (nid == block::WOODEN_DOOR || nid == block::IRON_DOOR) {
            updateDoorAt(level, server, nx, ny, nz);
        } else if (nid == block::PISTON_ID || nid == block::STICKY_PISTON) {
            updatePistonAt(level, server, nx, ny, nz);
        } else if ((nid == block::REDSTONE_TORCH_ON || nid == block::REDSTONE_TORCH_OFF)
                   && nx == x && nz == z && ny == y + 1) {
            // Misma restricción que en notifyNeighbors: sólo si el
            // wire ES el soporte de esa antorcha (está justo debajo).
            updateRedstoneTorchAt(level, server, nx, ny, nz);
        }
    }
}

void updateWireAt(Level* level, atomikpe::Server* server, int32_t x, int32_t y, int32_t z) {
    updateWireInternal(level, server, x, y, z, 0);
}

// ============================================================
// updateLampAt — la lámpara de redstone es sólida+oscura (id 123)
// cuando no recibe señal, y sólida+con luz (id 124) cuando sí. A
// diferencia del wire, no propaga nada más allá de sí misma — es un
// consumidor de señal, no un conductor — así que no hay riesgo de
// cascada aquí en absoluto.
// ============================================================
void updateLampAt(Level* level, atomikpe::Server* server, int32_t x, int32_t y, int32_t z) {
    if (!level) return;
    if (y < 0 || y >= 256) return;
    if (!level->isChunkLoaded(x, z)) return;
    uint8_t id = level->getBlockId(x, y, z);
    if (id != block::REDSTONE_LAMP && id != block::LIT_REDSTONE_LAMP) return;

    bool powered = isPowered(level, x, y, z);
    uint8_t wantId = powered ? block::LIT_REDSTONE_LAMP : block::REDSTONE_LAMP;
    if (wantId == id) return; // ya está en el estado correcto

    level->setBlock(x, y, z, wantId, 0);
    broadcastBlock(server, x, y, z, wantId, 0);
}

// ============================================================
// updateRedstoneTorchAt — la antorcha de redstone es un INVERSOR: se
// apaga cuando su bloque de soporte está powered, se enciende cuando
// no lo está. Solo se maneja la orientación "hacia abajo" (antorcha
// sobre un bloque, soporte = vecino de abajo), que es el caso que
// notifyNeighbors puede disparar hoy sin conocer la orientación real
// guardada en `data` (antorchas de pared quedan pendientes: requieren
// leer el bit de cara en `data`, que este motor no interpreta aún para
// torches — ver BlockIds.h, no hay constantes de cara para torch).
// No propaga más allá de sí misma en cascada — a diferencia del wire,
// cambiar una antorcha requiere que notifyNeighbors visite de nuevo
// sus vecinos wire/lámpara, cosa que ya ocurre porque quien llama a
// esta función (notifyNeighbors) también notifica al resto de vecinos
// del bloque que originó el cambio.
// ============================================================
void updateRedstoneTorchAt(Level* level, atomikpe::Server* server, int32_t x, int32_t y, int32_t z) {
    if (!level) return;
    if (y < 1 || y >= 256) return; // necesita soporte debajo; y=0 no tiene bloque debajo
    if (!level->isChunkLoaded(x, z)) return;
    uint8_t id = level->getBlockId(x, y, z);
    if (id != block::REDSTONE_TORCH_ON && id != block::REDSTONE_TORCH_OFF) return;

    // NOTA DE ALCANCE: esta función sólo decide ON vs OFF por señal.
    // No comprueba si el bloque de soporte fue removido por completo
    // (ej. alguien rompió el bloque de abajo) — en ese caso vanilla
    // real destruiría la antorcha y la dropearía como ítem. Ningún
    // bloque de este proyecto implementa hoy "destruirse al perder
    // soporte" (ni torch normal, ni carteles, ni placas de presión),
    // así que añadirlo sólo aquí sería inconsistente con el resto del
    // motor de bloques — se deja como extensión futura transversal
    // (física de soporte), no específica de redstone.

    // Soporte = el bloque inmediatamente debajo (x, y-1, z). Necesitamos
    // saber si ESE bloque recibe señal de sus vecinos — pero SIN contar
    // a la antorcha misma como uno de esos vecinos: la antorcha vive
    // justo encima del soporte, así que isPowered(soporte) la vería a
    // ella misma emitiendo 15 y la antorcha jamás podría apagarse (se
    // "auto-sostendría" encendida). Por eso este chequeo se escribe a
    // mano en vez de reusar isPowered() tal cual: revisamos los 5
    // vecinos del soporte que NO son la antorcha (los 4 laterales + el
    // de más abajo), exactamente igual que isPowered pero excluyendo
    // el vecino de arriba.
    int32_t sx = x, sy = y - 1, sz = z;
    if (!level->isChunkLoaded(sx, sz)) return;
    static const int32_t dx[5] = {1, -1, 0, 0, 0};
    static const int32_t dz[5] = {0, 0, 1, -1, 0};
    static const int32_t dy[5] = {0, 0, 0, 0, -1};
    bool supportPowered = false;
    for (int i = 0; i < 5; ++i) {
        int32_t nx = sx + dx[i], ny = sy + dy[i], nz = sz + dz[i];
        if (ny < 0 || ny >= 256) continue;
        if (!level->isChunkLoaded(nx, nz)) continue;
        if (getEmittedPower(level, nx, ny, nz) > 0) { supportPowered = true; break; }
    }
    // El soporte también puede ser él mismo un wire con potencia
    // (mismo caso que contempla isPowered para wire).
    if (!supportPowered) {
        uint8_t supportId = level->getBlockId(sx, sy, sz);
        if (supportId == block::REDSTONE_WIRE && (level->getBlockData(sx, sy, sz) & 0x0F) > 0) {
            supportPowered = true;
        }
    }

    uint8_t wantId = supportPowered ? block::REDSTONE_TORCH_OFF : block::REDSTONE_TORCH_ON;
    if (wantId == id) return;

    level->setBlock(x, y, z, wantId, 0);
    broadcastBlock(server, x, y, z, wantId, 0);
}

// ============================================================
// updateDoorAt — localiza la mitad inferior y decide el nuevo estado
// según isPowered(). Se puede llamar pasando cualquiera de las dos
// mitades — notifyNeighbors no sabe (ni necesita saber) cuál de las
// dos es.
//
// FORMATO DE `data` DE ESTE MOTOR (esquema propio, NO el legado de
// Java "5 bits en 4" — ver investigación: en vanilla Java el bit que
// marca mitad superior/inferior es ambiguo entre versiones y fue
// fuente de bugs históricos en puertas dobles. Como este servidor no
// necesita ser compatible con saves de Java, se define un esquema
// simple y sin ambigüedad):
//   - Bit 0x08: SIEMPRE "es mitad superior" — presente en AMBAS
//     mitades, nunca reutilizado para otra cosa. 0 = inferior,
//     1 = superior. Así, leer el bit 0x08 de un bloque de puerta dice
//     inequívocamente cuál mitad es, sin depender de en qué mitad se
//     está mirando.
//   - Bit 0x04: "abierta/cerrada" — sólo tiene significado en la
//     mitad INFERIOR (donde 0x08=0); es el mismo bit que ya usan
//     BlockDoor::isOpen()/toggle() en block/BlockDoor.h, así que
//     ambas piezas del proyecto están de acuerdo. En la mitad superior
//     este bit no se define ni se lee.
//   - Bits 0x01-0x02: orientación/bisagra, fijados al colocar
//     (Player.cpp) y nunca tocados por este motor.
// ============================================================
void updateDoorAt(Level* level, atomikpe::Server* server, int32_t x, int32_t y, int32_t z) {
    if (!level) return;
    if (y < 0 || y >= 256) return;
    if (!level->isChunkLoaded(x, z)) return;
    uint8_t id = level->getBlockId(x, y, z);
    if (id != block::WOODEN_DOOR && id != block::IRON_DOOR) return;

    uint8_t data = level->getBlockData(x, y, z);
    bool isTopHalf = (data & 0x08) != 0;
    int32_t lowerY = isTopHalf ? (y - 1) : y;
    if (lowerY < 0 || lowerY >= 256) return;
    if (!level->isChunkLoaded(x, z)) return;
    uint8_t lowerId = level->getBlockId(x, lowerY, z);
    if (lowerId != id) return; // la mitad inferior no coincide (puerta rota a medias / desincronizada) — no tocar nada

    uint8_t lowerData = level->getBlockData(x, lowerY, z);
    if ((lowerData & 0x08) != 0) return; // salvaguarda: lo que creíamos "inferior" también trae el bit de superior — datos corruptos, no tocar

    bool currentlyOpen = (lowerData & 0x04) != 0;
    bool shouldBeOpen = isPowered(level, x, lowerY, z);
    if (shouldBeOpen == currentlyOpen) return; // sin cambio

    uint8_t newLowerData = lowerData ^ 0x04;
    level->setBlock(x, lowerY, z, id, newLowerData);
    broadcastBlock(server, x, lowerY, z, id, newLowerData);

    // La mitad superior de este motor solo guarda el bit 0x08 (es
    // superior) + orientación/bisagra en 0x01-0x02 — nunca guarda el
    // bit de abierta (ver esquema documentado arriba), así que
    // visualmente no necesita cambiar su `data` al abrir/cerrar. El
    // cliente MCPE anima la puerta completa a partir del cambio de la
    // mitad inferior únicamente. No se reescribe la mitad superior
    // aquí para no arriesgar perder su bit de bisagra por descuido.
}

// ============================================================
// updatePistonAt — extiende/retrae según isPowered(). Direcciones
// (bits 0-2 de `data`, convención vanilla real confirmada): 0=abajo,
// 1=arriba, 2=norte(-Z), 3=sur(+Z), 4=oeste(-X), 5=este(+X). Bit 0x08
// = extendido.
// ============================================================
static void pistonDirOffset(uint8_t dirBits, int32_t& ox, int32_t& oy, int32_t& oz) {
    ox = oy = oz = 0;
    switch (dirBits & 0x07) {
        case 0: oy = -1; break; // abajo
        case 1: oy = 1;  break; // arriba
        case 2: oz = -1; break; // norte
        case 3: oz = 1;  break; // sur
        case 4: ox = -1; break; // oeste
        case 5: ox = 1;  break; // este
        default: break; // 6/7 no son direcciones válidas en vanilla — no-op seguro
    }
}

void updatePistonAt(Level* level, atomikpe::Server* server, int32_t x, int32_t y, int32_t z) {
    if (!level) return;
    if (y < 0 || y >= 256) return;
    if (!level->isChunkLoaded(x, z)) return;
    uint8_t id = level->getBlockId(x, y, z);
    if (id != block::PISTON_ID && id != block::STICKY_PISTON) return;

    uint8_t data = level->getBlockData(x, y, z);
    bool currentlyExtended = (data & 0x08) != 0;
    bool shouldExtend = isPowered(level, x, y, z);
    if (shouldExtend == currentlyExtended) return; // sin cambio

    int32_t ox, oy, oz;
    pistonDirOffset(data, ox, oy, oz);
    int32_t headX = x + ox, headY = y + oy, headZ = z + oz;
    if (headY < 0 || headY >= 256) return; // el cabezal caería fuera del mundo — no-op seguro
    if (!level->isChunkLoaded(headX, headZ)) return;

    if (shouldExtend) {
        // Sólo empuja si el destino es aire puro. Empujar fluidos u
        // otros bloques "reemplazables" (fuego, wire, etc.) requiere
        // reglas propias por tipo que este motor no cubre aún — se
        // deja documentado como extensión futura en vez de arriesgar
        // un comportamiento incorrecto (dropear algo que en vanilla no
        // dropea, o mover un fluido como si fuera sólido).
        uint8_t destId = level->getBlockId(headX, headY, headZ);
        // Bloqueado — el pistón no se extiende, igual que en vanilla. No
        // hace falta recordar "señal pendiente": si luego alguien rompe
        // el bloque que bloquea, esa rotura llama a notifyNeighbors en
        // (headX,headY,headZ), que revisa SUS 6 vecinos — y el pistón
        // en (x,y,z) es uno de ellos — así que se re-evalúa solo, sin
        // necesitar un scheduler ni un estado extra.
        if (destId != 0) return;

        uint8_t newData = data | 0x08;
        level->setBlock(x, y, z, id, newData);
        broadcastBlock(server, x, y, z, id, newData);

        level->setBlock(headX, headY, headZ, block::PISTON_HEAD, data & 0x07);
        broadcastBlock(server, headX, headY, headZ, block::PISTON_HEAD, data & 0x07);
        // No se llama a notifyNeighbors sobre el cabezal: en vanilla
        // real el piston head es un bloque técnico que no interactúa
        // con redstone (no es fuente ni conductor). Un wire detrás del
        // cabezal quedaría sin recalcular hasta el próximo evento que
        // lo toque directamente — caso extremadamente raro en
        // circuitos reales y documentado aquí en vez de ampliar el
        // alcance de esta función sin necesidad.
    } else {
        uint8_t newData = data & ~0x08;
        level->setBlock(x, y, z, id, newData);
        broadcastBlock(server, x, y, z, id, newData);

        // Retraer: el cabezal vuelve a aire. Un pistón pegajoso en
        // vanilla intentaría además arrastrar de vuelta el bloque que
        // había empujado — no implementado aquí (requiere recordar qué
        // se empujó, no solo dónde quedó el cabezal); queda como
        // extensión futura documentada.
        uint8_t headId = level->getBlockId(headX, headY, headZ);
        if (headId == block::PISTON_HEAD) {
            level->setBlock(headX, headY, headZ, 0, 0);
            broadcastBlock(server, headX, headY, headZ, 0, 0);
        }
    }
}

// ============================================================
// notifyNeighbors — punto de entrada principal. Se llama tras romper o
// colocar un bloque en (x,y,z); revisa los 6 vecinos y dispara su
// actualización si son componentes de redstone.
// ============================================================
void notifyNeighbors(Level* level, atomikpe::Server* server, int32_t x, int32_t y, int32_t z) {
    if (!level) return;
    static const int32_t dx[6] = {1, -1, 0, 0, 0, 0};
    static const int32_t dy[6] = {0, 0, 1, -1, 0, 0};
    static const int32_t dz[6] = {0, 0, 0, 0, 1, -1};

    // El propio bloque, si es wire (por ejemplo se acaba de colocar
    // sobre una fuente), lámpara, antorcha, puerta o pistón, también
    // necesita recalcularse.
    if (y >= 0 && y < 256 && level->isChunkLoaded(x, z)) {
        uint8_t selfId = level->getBlockId(x, y, z);
        if (selfId == block::REDSTONE_WIRE) {
            updateWireAt(level, server, x, y, z);
        } else if (selfId == block::REDSTONE_LAMP || selfId == block::LIT_REDSTONE_LAMP) {
            updateLampAt(level, server, x, y, z);
        } else if (selfId == block::REDSTONE_TORCH_ON || selfId == block::REDSTONE_TORCH_OFF) {
            updateRedstoneTorchAt(level, server, x, y, z);
        } else if (selfId == block::WOODEN_DOOR || selfId == block::IRON_DOOR) {
            updateDoorAt(level, server, x, y, z);
        } else if (selfId == block::PISTON_ID || selfId == block::STICKY_PISTON) {
            updatePistonAt(level, server, x, y, z);
        }
    }

    for (int i = 0; i < 6; ++i) {
        int32_t nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
        if (ny < 0 || ny >= 256) continue;
        if (!level->isChunkLoaded(nx, nz)) continue;
        uint8_t nid = level->getBlockId(nx, ny, nz);
        if (nid == block::REDSTONE_WIRE) {
            updateWireAt(level, server, nx, ny, nz);
        } else if (nid == block::REDSTONE_LAMP || nid == block::LIT_REDSTONE_LAMP) {
            updateLampAt(level, server, nx, ny, nz);
        } else if (nid == block::REDSTONE_TORCH_ON || nid == block::REDSTONE_TORCH_OFF) {
            // La antorcha reacciona a cambios en su SOPORTE (el bloque
            // de abajo), no a cualquier vecino lateral — en vanilla
            // real un cambio de señal lateral no la afecta. Sólo la
            // recalculamos aquí si el vecino modificado ES su soporte,
            // es decir, si estamos mirando "hacia arriba" desde (x,y,z)
            // hacia la antorcha (nx,ny,nz) = (x,y+1,z).
            if (nx == x && nz == z && ny == y + 1) {
                updateRedstoneTorchAt(level, server, nx, ny, nz);
            }
        } else if (nid == block::WOODEN_DOOR || nid == block::IRON_DOOR) {
            updateDoorAt(level, server, nx, ny, nz);
        } else if (nid == block::PISTON_ID || nid == block::STICKY_PISTON) {
            updatePistonAt(level, server, nx, ny, nz);
        }
        // Palancas, botones, placas de presión no necesitan
        // recalcularse a sí mismas por un cambio de vecino — su estado
        // lo cambia directamente la interacción del jugador.
    }
}

} // namespace atomikpe::redstone
