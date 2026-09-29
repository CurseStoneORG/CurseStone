#pragma once
#include "BlockTorch.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockRedstoneTorch : public BlockTorch {
public:
    // FIX: "REDSTONE_TORCH" (sin sufijo) no existe en BlockIds.h — solo
    // REDSTONE_TORCH_OFF (75) y REDSTONE_TORCH_ON (76), que son dos
    // bloques con id distinto (igual que MCPE/vanilla los modelan:
    // encender/apagar una antorcha de redstone cambia su id de bloque,
    // no solo su `data`). Esta clase representa el estado apagado —
    // BlockRedstoneTorchOn (si se necesita como clase aparte) usaría
    // REDSTONE_TORCH_ON. La lógica de encendido/apagado real la maneja
    // RedstoneManager operando directamente sobre el id del mundo, no
    // esta jerarquía (que actualmente no está conectada al flujo de
    // juego — ver BlockRegistry.cpp, donde nada llama a Block::get()).
    uint8_t id() const override { return REDSTONE_TORCH_OFF; }
    std::string name() const override { return "Redstone Torch"; }
};
} // namespace
