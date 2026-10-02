// TileFurnace.cpp — horno completo (PMMP Tile/Furnace.php):
// consume fuel, funde el input según el CraftingManager, escribe el
// resultado en el slot 2 y sincroniza progreso/contenido con el
// cliente mientras haya alguien mirando.
#include "TileFurnace.h"
#include "Player.h"
#include "inventory/FurnaceInventory.h"
#include "inventory/CraftingManager.h"
#include "Server.h"
#include "network/protocol/ContainerSetContentPacket.h"
#include "network/protocol/ContainerSetDataPacket.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/ListTag.h"
#include <algorithm>
namespace cursestone::tile {
TileFurnace::TileFurnace(cursestone::level::Position pos)
    : Tile("Furnace", pos),
      inv_(std::make_unique<cursestone::inventory::FurnaceInventory>()) {}
TileFurnace::~TileFurnace() = default;
void TileFurnace::setItem(int slot, const cursestone::item::ItemStack& it) {
    if (slot < 0 || slot >= 3) return;
    inv_->setItem(slot, it);
    dirty_ = true;
}
cursestone::item::ItemStack TileFurnace::getItem(int slot) const {
    if (slot < 0 || slot >= 3) return cursestone::item::ItemStack::EMPTY();
    return inv_->getItem(slot);
}
void TileFurnace::removeItem(int slot) {
    if (slot < 0 || slot >= 3) return;
    inv_->clear(slot);
    dirty_ = true;
}
void TileFurnace::addViewer(void* player) {
    if (player && std::find(viewers_.begin(), viewers_.end(), player) == viewers_.end())
        viewers_.push_back(player);
}
void TileFurnace::removeViewer(void* player) {
    viewers_.erase(std::remove(viewers_.begin(), viewers_.end(), player),
                   viewers_.end());
}
// ============================================================
// Fuel — valores vanilla de PMMP/Genisys (Fuel.php).
// Nota MCPE 0.15: lava bucket = id 327 (ItemIds::LAVA_BUCKET);
// bucket vacío = 325. El cubo de lava rinde 20000 ticks y al
// consumirse deja un cubo vacío en el slot de fuel.
// ============================================================
namespace {
constexpr int COAL_BURN = 1600;       // coal/charcoal (id 263, any meta)
constexpr int LAVA_BUCKET_BURN = 20000; // lava bucket (id 327)
constexpr int BLAZE_ROD_BURN = 2400;  // blaze rod (id 369)
constexpr int SAPLING_BURN = 100;     // sapling (id 6, any meta)
} // namespace

int TileFurnace::fuelBurnTicks(uint16_t id) {
    switch (id) {
        case 263: return COAL_BURN;
        case 327: return LAVA_BUCKET_BURN;
        case 369: return BLAZE_ROD_BURN;
        case 6:   return SAPLING_BURN;
        default:  return 0;
    }
}
// ============================================================
// Fundición — delega en CraftingManager (recetas RECIPE_FURNACE).
// ============================================================
bool TileFurnace::smeltResult(const cursestone::item::ItemStack& in,
                              cursestone::item::ItemStack& out) const {
    auto& srv = cursestone::Server::getInstance();
    // craftingManager() se agrega a Server este mismo cambio (antes el
    // manager era un miembro privado sin accessor).
    auto* mgr = srv.craftingManager();
    if (!mgr || in.isEmpty()) return false;
    const inventory::Recipe* r =
        mgr->matchFurnaceWithMeta(in.id, in.meta);
    if (!r) r = mgr->matchFurnace(in.id);
    if (!r) return false;
    out = r->result();
    if (out.count <= 0) out.count = 1;
    return true;
}
// ============================================================
// tick — el corazón del horno.
// ============================================================
void TileFurnace::tick(uint64_t) {
    auto input  = inv_->getItem(SLOT_INPUT);
    auto fuel   = inv_->getItem(SLOT_FUEL);
    auto result = inv_->getItem(SLOT_RESULT);
    cursestone::item::ItemStack smelted;
    const bool canSmelt =
        !input.isEmpty() && smeltResult(input, smelted) &&
        (result.isEmpty() ||
         (result.id == smelted.id && result.meta == smelted.meta &&
          result.count + smelted.count <= 64));
    if (burn_time_ > 0) {
        --burn_time_;
    }
    // Encender fuel si: no hay burn activo, se puede fundir, y hay
    // fuel consumible (PMMP: checkUsedFuel).
    if (burn_time_ <= 0 && canSmelt && !fuel.isEmpty()) {
        int ticks = fuelBurnTicks(fuel.id);
        if (ticks > 0) {
            burn_time_ = ticks;
            max_burn_time_ = ticks;
            fuel.count -= 1;
            if (fuel.count <= 0) {
                // El cubo de lava deja un cubo vacío al consumirse
                // (PMMP Furnace.php: "checkUsedFuel").
                if (fuel.id == 327) {
                    fuel.id = 325;
                    fuel.count = 1;
                    inv_->setItem(SLOT_FUEL, fuel);
                } else {
                    inv_->setItem(SLOT_FUEL, cursestone::item::ItemStack::EMPTY());
                }
            } else {
                inv_->setItem(SLOT_FUEL, fuel);
            }
            dirty_ = true;
        }
    }
    // Fundición propiamente dicha.
    if (burn_time_ > 0 && canSmelt) {
        ++cook_time_;
        if (cook_time_ >= maxCookTime()) {
            cook_time_ = 0;
            // 1 input → 1 result (PMMP: onContentsChange).
            input.count -= 1;
            if (input.count <= 0) input = cursestone::item::ItemStack::EMPTY();
            inv_->setItem(SLOT_INPUT, input);
            if (result.isEmpty()) {
                result = smelted;
            } else {
                result.count += smelted.count;
            }
            inv_->setItem(SLOT_RESULT, result);
            dirty_inventory_ = true;
            dirty_ = true;
        }
    } else {
        cook_time_ = 0;
    }
    // Reenviar contenido solo cuando cambió y hay viewers.
    if (dirty_inventory_ && !viewers_.empty()) {
        auto content =
            std::make_shared<network::protocol::ContainerSetContentPacket>();
        content->windowid = Player::FURNACE_WINDOW_ID;
        content->slots.resize(3);
        for (int i = 0; i < 3; ++i) content->slots[i] = inv_->getItem(i);
        for (void* v : viewers_) {
            // v es un Player* — el cast estático es válido porque solo
            // Player::openFurnaceAt registra viewers y Player nunca es
            // otra cosa. sendPacket es virtual via Player.
            static_cast<cursestone::Player*>(v)->sendPacket(content);
        }
    }
    dirty_inventory_ = false;
    // Progreso de UI (solo con viewers; barato: 2 varints por viewer).
    if (!viewers_.empty()) sendProgressToViewers();
}
void TileFurnace::sendProgressToViewers() {
    // Los viewers son Player* crudos: un jugador puede desconectarse
    // (window cerrado via closeOpenChest en Player::close) sin que este
    // tile lo sepa — filtrar muertos antes de mandar nada.
    viewers_.erase(
        std::remove_if(viewers_.begin(), viewers_.end(),
                       [](void* v) {
                           auto* p = static_cast<cursestone::Player*>(v);
                           return !p || !p->isConnected();
                       }),
        viewers_.end());

    auto pk = std::make_shared<network::protocol::ContainerSetDataPacket>();
    pk->windowid = cursestone::Player::FURNACE_WINDOW_ID;
    pk->property = network::protocol::ContainerSetDataPacket::PROPERTY_FURNACE_TICKS;
    pk->value = std::clamp(cook_time_, 0, 200);
    for (void* v : viewers_)
        static_cast<cursestone::Player*>(v)->sendPacket(pk);
    auto pk2 = std::make_shared<network::protocol::ContainerSetDataPacket>();
    pk2->windowid = cursestone::Player::FURNACE_WINDOW_ID;
    pk2->property = network::protocol::ContainerSetDataPacket::PROPERTY_FURNACE_FUEL;
    pk2->value = max_burn_time_ > 0
        ? std::clamp((burn_time_ * 200) / max_burn_time_, 0, 200) : 0;
    for (void* v : viewers_)
        static_cast<cursestone::Player*>(v)->sendPacket(pk2);
}
// ============================================================
// Persistencia — formato legacy de Nukkit {BurnDuration, BurnTicks,
// CookTicks, Items:[{Slot,id,Damage,Count}]}.
// ============================================================
void TileFurnace::writeNBT(mcp_tag_t* tag) const {
    if (!tag) return;
    Tile::writeNBT(tag);  // id/x/y/z
    mcp_compound_tag_set_short(tag, "BurnDuration",
        static_cast<int16_t>(max_burn_time_));
    mcp_compound_tag_set_short(tag, "BurnTicks",
        static_cast<int16_t>(burn_time_));
    mcp_compound_tag_set_short(tag, "CookTicks",
        static_cast<int16_t>(cook_time_));
    mcp_tag_t* items = mcp_list_tag_new_named("Items", MCP_TAG_COMPOUND);
    for (int slot = 0; slot < 3; ++slot) {
        auto stack = inv_->getItem(slot);
        if (stack.isEmpty()) continue;
        mcp_tag_t* entry = mcp_compound_tag_new();
        mcp_compound_tag_set_byte(entry, "Slot", static_cast<int8_t>(slot));
        mcp_compound_tag_set_short(entry, "id", static_cast<int16_t>(stack.id));
        mcp_compound_tag_set_short(entry, "Damage", static_cast<int16_t>(stack.meta));
        mcp_compound_tag_set_byte(entry, "Count", static_cast<int8_t>(stack.count));
        mcp_list_tag_append(items, entry);
    }
    mcp_compound_tag_set(tag, "Items", items);
}
void TileFurnace::readNBT(const mcp_tag_t* tag) {
    if (!tag) return;
    Tile::readNBT(tag);
    burn_time_ = mcp_compound_tag_get_short(tag, "BurnTicks", 0);
    max_burn_time_ = mcp_compound_tag_get_short(tag, "BurnDuration", 0);
    cook_time_ = mcp_compound_tag_get_short(tag, "CookTicks", 0);
    const mcp_tag_t* items = mcp_compound_tag_get(tag, "Items");
    if (!items || mcp_tag_get_type(items) != MCP_TAG_LIST) return;
    size_t count = mcp_list_tag_size(items);
    for (size_t i = 0; i < count; ++i) {
        const mcp_tag_t* entry = mcp_list_tag_get(items, i);
        if (!entry || mcp_tag_get_type(entry) != MCP_TAG_COMPOUND) continue;
        int slot = mcp_compound_tag_get_byte(entry, "Slot", -1);
        if (slot < 0 || slot >= 3) continue;
        cursestone::item::ItemStack stack;
        stack.id = static_cast<uint16_t>(mcp_compound_tag_get_short(entry, "id", 0));
        stack.meta = static_cast<uint8_t>(mcp_compound_tag_get_short(entry, "Damage", 0));
        stack.count = mcp_compound_tag_get_byte(entry, "Count", 0);
        if (stack.isEmpty()) continue;
        inv_->setItem(slot, stack);
    }
    dirty_ = false;
}
} // namespace
