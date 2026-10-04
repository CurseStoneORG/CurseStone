// RedstoneManager.h — motor de propagación de señal de redstone.
//
// DISEÑO (aprendiendo del incidente de BlockFire v6: un freeze de 88s
// causado por propagación en cascada sin límite duro):
//   - Toda propagación de wire está acotada a un máximo de 15 pasos de
//     potencia (regla real de Minecraft: la señal decae 1 por bloque y
//     nunca viaja más de 15 bloques desde la fuente). Esto es un límite
//     matemático duro, no una heurística — nunca puede iterar sin fin.
//   - No hay tareas programadas (scheduler) recurrentes que se
//     re-encolen solas. Cada actualización de redstone es un cálculo
//     síncrono y acotado que termina en el mismo tick en que se pide.
//   - notifyNeighbors() es la única función que el resto del código
//     debe llamar (desde Player.cpp, tras romper/colocar un bloque).
//     Nunca se llama a sí misma de forma indirecta más allá de la
//     propagación acotada del wire.
//
// FIX (reportado por el usuario tras probar la primera versión): el
// motor cambiaba el `data`/id del bloque en el World correctamente,
// pero nunca avisaba a los jugadores conectados — por eso el circuito
// solo se veía "prendido" al reconectar (momento en que el cliente
// vuelve a pedir los chunks desde cero), y nada que dependiera de la
// señal (lámparas) reaccionaba en vivo. Las funciones que cambian
// bloques ahora reciben también un Server*, y hacen el mismo broadcast
// de UpdateBlockPacket (+ sonido/partícula cuando aplica) que ya usa
// Player.cpp para romper/colocar — mismos jugadores, mismo patrón,
// para que la propagación se vea instantánea como en MCPE real.
#pragma once
#include <cstdint>

namespace cursestone::world { class World; }
namespace cursestone { class Server; }

namespace cursestone::redstone {

// Se llama tras romper o colocar CUALQUIER bloque en (x,y,z). Revisa los
// 6 vecinos inmediatos y, si alguno es un componente de redstone
// (wire, antorcha, palanca, botón, placa de presión, bloque de
// redstone), dispara su actualización correspondiente — y transmite
// cada cambio visible a los jugadores que estén viendo ese chunk.
//
// Esta es la función principal que el resto del proyecto necesita
// llamar — todo lo demás en este módulo es implementación interna.
void notifyNeighbors(cursestone::world::World* world, cursestone::Server* server,
                      int32_t x, int32_t y, int32_t z);

// Recalcula la potencia (0-15) que debería tener un bloque de redstone
// wire en (x,y,z) dado su entorno actual, y actualiza su `data` en el
// mundo si cambió — propagando en cascada ACOTADA (máx. 15 pasos) a los
// wires vecinos si el cambio de potencia los afecta a ellos también.
// Transmite cada wire que cambie a los jugadores cercanos.
void updateWireAt(cursestone::world::World* world, cursestone::Server* server,
                   int32_t x, int32_t y, int32_t z);

// Recalcula si una lámpara de redstone en (x,y,z) debe estar encendida
// (id LIT_REDSTONE_LAMP) o apagada (id REDSTONE_LAMP) según si recibe
// señal actualmente, y transmite el cambio si corresponde. A diferencia
// del wire, la lámpara solo consume señal, nunca la propaga — no hay
// riesgo de cascada al llamar esto.
void updateLampAt(cursestone::world::World* world, cursestone::Server* server,
                   int32_t x, int32_t y, int32_t z);

// Devuelve la potencia de señal (0-15) que el bloque en (x,y,z) aporta
// hacia afuera — considera antorchas encendidas, bloques de redstone,
// wire ya calculado, y palancas/botones activados. Devuelve 0 si el
// bloque no es una fuente de señal o está apagado. No cambia nada en
// el mundo, así que no necesita Server*.
int getEmittedPower(cursestone::world::World* world, int32_t x, int32_t y, int32_t z);

// True si el bloque en (x,y,z) está actualmente recibiendo señal de
// redstone (potencia > 0) desde cualquiera de sus vecinos o desde sí
// mismo (para wire). Es lo que consultan puertas/pistones/lámparas para
// decidir si deben activarse.
bool isPowered(cursestone::world::World* world, int32_t x, int32_t y, int32_t z);

// Intenta interactuar con un componente de redstone accionable por el
// jugador (palanca, botón, puerta de madera — la de hierro NO
// reacciona a clic, sólo a redstone, igual que en vanilla) en
// (x,y,z). Si el bloque en esa posición es uno de esos tipos,
// alterna/activa su estado, actualiza el mundo, transmite el cambio
// y devuelve true. Palanca/botón además notifican a los vecinos
// (pueden alimentar un circuito); la puerta NO lo hace al recibir
// clic manual — ver nota en el .cpp sobre por qué. Si no es un
// componente accionable, no hace nada y devuelve false — así el
// llamador (Player) sabe si debe seguir con su propia lógica de
// colocación de item.
bool tryInteract(cursestone::world::World* world, cursestone::Server* server,
                  int32_t x, int32_t y, int32_t z);

// Recalcula si una antorcha de redstone en (x,y,z) debe estar
// encendida (id REDSTONE_TORCH_ON) o apagada (id REDSTONE_TORCH_OFF).
// Comportamiento vanilla real (la antorcha es un INVERSOR lógico, no un
// simple foco): se apaga si el bloque que la sostiene (el vecino hacia
// donde apunta, normalmente abajo) está recibiendo señal, y se enciende
// si ese bloque de soporte NO tiene señal. Esto es lo que hace posible
// que la antorcha actúe como NOT gate en circuitos reales. Solo maneja
// antorchas orientadas hacia abajo (colocadas sobre un bloque) — es el
// caso que cubre notifyNeighbors hoy; antorchas en pared quedan como
// extensión futura documentada (ver comentario en el .cpp).
void updateRedstoneTorchAt(cursestone::world::World* world, cursestone::Server* server,
                             int32_t x, int32_t y, int32_t z);

// Recalcula si una puerta (madera o hierro) debe estar abierta o
// cerrada según si su mitad INFERIOR está recibiendo señal de
// redstone. La puerta ocupa 2 bloques verticales; sólo la mitad
// inferior guarda el bit de orientación+abierta (0x04) y sólo ella se
// consulta para señal — igual que en vanilla. Si (x,y,z) es la mitad
// superior, la función localiza la inferior automáticamente (y-1) para
// que notifyNeighbors pueda llamarla con cualquiera de las dos mitades
// sin distinguir cuál es cuál. Sincroniza el bit 0x04 en ambas mitades
// y transmite el cambio de las que hayan cambiado.
void updateDoorAt(cursestone::world::World* world, cursestone::Server* server,
                   int32_t x, int32_t y, int32_t z);

// Recalcula si un pistón (normal o pegajoso) en (x,y,z) debe estar
// extendido o retraído según si está recibiendo señal de redstone.
// Al extender: si el bloque inmediatamente enfrente (según la
// dirección guardada en los bits 0-2 de `data`) es aire o un bloque
// reemplazable, coloca ahí un PISTON_HEAD y empuja ese contenido fuera
// (lo reemplaza por aire, sin generar drop — igual que vanilla, que no
// dropea el bloque empujado, lo reubica). Empuje limitado a UN bloque
// simple no adyacente a otro sólido: cadenas largas de bloques
// empujados y el límite de 12 bloques de vanilla quedan como extensión
// futura documentada (ver comentario en el .cpp) — cubre el circuito
// típico (pistón empujando 1 bloque) sin arriesgar recorrer cadenas
// sin límite. Al retraer: quita el PISTON_HEAD (vuelve a aire); un
// pistón pegajoso además intentaría traer de vuelta el bloque
// empujado, lo cual también queda documentado como extensión futura.
void updatePistonAt(cursestone::world::World* world, cursestone::Server* server,
                     int32_t x, int32_t y, int32_t z);

} // namespace cursestone::redstone
