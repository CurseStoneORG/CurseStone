// ItemFlintSteel.h — Portado de pocketmine/item/FlintSteel.php (PMMP)
//
// Implementa:
//   - Colocación de fuego en bloques sólidos (onActivate)
//   - Activación de portal del Nether con frame de obsidiana (4-23 ancho, 5-23 alto)
//   - Degradación de durabilidad (2 puntos por uso en survival)
//   - Detección de dirección del portal (eje X o eje Z)
//
// Diferencias con el PHP original:
//   - onActivate() recibe Level* + Player* + coordenadas enteras en vez de
//     objetos Block con posición embebida.
//   - La inserción de bloques Portal usa Level::setBlock(x,y,z, PORTAL, 0).
//     BlockPortal (id=90 en MCPE 0.15.x) debe estar registrado en Block::registry().
//   - La durabilidad se guarda en el campo `meta` del ItemStack (mirrors PMMP
//     Tool::useOn($block, $damage=2)).
#pragma once
#include "Item.h"
#include <string>
#include <cstdint>

namespace cursestone {
    class Player;
    namespace level { class Level; }
}

namespace cursestone::item {

class ItemFlintSteel : public Item {
public:
    ItemFlintSteel() : Item(259) {}
    std::string name() const override { return "Flint and Steel"; }

    // Flint & Steel es una herramienta (toma durabilidad).
    bool isTool() const override { return true; }
    int maxStackSize() const override { return 1; }

    // Durabilidad máxima: 65 usos (igual que PMMP MCPE 0.15.x)
    static constexpr uint8_t MAX_DURABILITY = 64;

    // Punto de entrada principal — llamar desde Player::handleBlockPlace()
    // cuando heldItemId == 259.
    //
    // Mirrors: FlintSteel::onActivate($level, $player, $block, $target, $face, ...)
    //
    // Parámetros:
    //   level         — mundo activo
    //   player        — jugador que activó el ítem
    //   bx,by,bz      — coordenadas del bloque AIR en la cara clickeada
    //   tx,ty,tz      — coordenadas del bloque TARGET (el que recibió el click)
    //   face          — cara del target (0=abajo, 1=arriba, 2-5=laterales)
    //
    // Retorna true si se realizó alguna acción (propagando la respuesta al cliente).
    static bool onActivate(level::Level* level, Player* player,
                           int32_t bx, int32_t by, int32_t bz,   // bloque AIR
                           int32_t tx, int32_t ty, int32_t tz,   // bloque target
                           uint8_t face);

private:
    // Aplica 2 puntos de daño al ítem en mano del jugador (survival only).
    // mirrors: $this->useOn($block, 2)
    static void applyDurability(Player* player);

    // Intenta formar un portal del Nether sobre el bloque de obsidiana en (tx,ty,tz).
    // Verifica el rectángulo en el eje X primero, luego en Z.
    // Retorna true si el portal fue creado.
    // mirrors: la lógica de detección x-direction / z-direction de FlintSteel.php
    static bool tryCreateNetherPortal(level::Level* level, Player* player,
                                      int32_t tx, int32_t ty, int32_t tz);

    // Verifica si hay un marco de obsidiana válido en el plano XY a Z=tz fijo.
    // `xMin/xMax/yMax` son los límites calculados del rectángulo.
    // Retorna true si el techo está completo.
    static bool checkPortalFrameX(level::Level* level,
                                  int32_t xMin, int32_t xMax,
                                  int32_t yBase, int32_t yMax,
                                  int32_t tz);

    // Ídem para el plano ZY a X=tx fijo.
    static bool checkPortalFrameZ(level::Level* level,
                                  int32_t tx,
                                  int32_t zMin, int32_t zMax,
                                  int32_t yBase, int32_t yMax);

    // Rellena el interior del portal con bloques PORTAL.
    static void fillPortalX(level::Level* level,
                            int32_t xMin, int32_t xMax,
                            int32_t yBase, int32_t yMax,
                            int32_t tz);

    static void fillPortalZ(level::Level* level,
                            int32_t tx,
                            int32_t zMin, int32_t zMax,
                            int32_t yBase, int32_t yMax);
};

} // namespace cursestone::item
