#include "Item.h"
#include "ItemTool.h"
#include "ItemArmor.h"
#include "ItemFood.h"
#include "ItemApple.h"
#include "ItemBread.h"
#include "ItemRawBeef.h"
#include "ItemSteak.h"
#include "ItemRawPorkchop.h"
#include "ItemCookedPorkchop.h"
#include "ItemRawChicken.h"
#include "ItemCookedChicken.h"
#include "ItemCarrot.h"
#include "ItemMelon.h"
#include "ItemGoldenApple.h"
#include "ItemDiamond.h"
#include "ItemIronIngot.h"
#include "ItemGoldIngot.h"
#include "ItemCoal.h"
#include "ItemStick.h"
#include "ItemFlint.h"
#include "ItemFeather.h"
#include "ItemLeather.h"
#include "ItemGunpowder.h"
#include "ItemEmerald.h"
#include "ItemRedstone.h"
#include "ItemSugar.h"
#include "ItemArrow.h"
#include "ItemSnowball.h"
#include "ItemEgg.h"
#include "ItemSign.h"
#include "ItemBucket.h"
#include "ItemWheat.h"
#include "ItemWheatSeeds.h"
#include "ItemSugarcane.h"
#include "ItemBow.h"
#include "ItemFlintSteel.h"
#include "ItemShears.h"
#include "ItemAxeWooden.h"
#include "ItemAxeStone.h"
#include "ItemAxeIron.h"
#include "ItemAxeGolden.h"
#include "ItemAxeDiamond.h"
#include "ItemPickaxeWooden.h"
#include "ItemPickaxeStone.h"
#include "ItemPickaxeIron.h"
#include "ItemPickaxeGolden.h"
#include "ItemPickaxeDiamond.h"
#include "ItemShovelWooden.h"
#include "ItemShovelStone.h"
#include "ItemShovelIron.h"
#include "ItemShovelGolden.h"
#include "ItemShovelDiamond.h"
#include "ItemSwordWooden.h"
#include "ItemSwordStone.h"
#include "ItemSwordIron.h"
#include "ItemSwordGolden.h"
#include "ItemSwordDiamond.h"
#include "ItemHoeWooden.h"
#include "ItemHoeStone.h"
#include "ItemHoeIron.h"
#include "ItemHoeGolden.h"
#include "ItemHoeDiamond.h"
#include "ItemIronHelmet.h"
#include "ItemIronChestplate.h"
#include "ItemIronLeggings.h"
#include "ItemIronBoots.h"
#include "ItemDiamondHelmet.h"
#include "ItemDiamondChestplate.h"
#include "ItemDiamondLeggings.h"
#include "ItemDiamondBoots.h"
#include "ItemLeatherHelmet.h"
#include "ItemLeatherChestplate.h"
#include "ItemLeatherLeggings.h"
#include "ItemLeatherBoots.h"
#include "ItemGoldHelmet.h"
#include "ItemGoldChestplate.h"
#include "ItemGoldLeggings.h"
#include "ItemGoldBoots.h"
#include <memory>
namespace mcppeserver::item {
void Item::registerAll() {
    registerItem(std::make_unique<ItemApple>());
    registerItem(std::make_unique<ItemBread>());
    registerItem(std::make_unique<ItemRawBeef>());
    registerItem(std::make_unique<ItemSteak>());
    registerItem(std::make_unique<ItemRawPorkchop>());
    registerItem(std::make_unique<ItemCookedPorkchop>());
    registerItem(std::make_unique<ItemRawChicken>());
    registerItem(std::make_unique<ItemCookedChicken>());
    registerItem(std::make_unique<ItemCarrot>());
    registerItem(std::make_unique<ItemMelon>());
    registerItem(std::make_unique<ItemGoldenApple>());
    registerItem(std::make_unique<ItemDiamond>());
    registerItem(std::make_unique<ItemIronIngot>());
    registerItem(std::make_unique<ItemGoldIngot>());
    registerItem(std::make_unique<ItemCoal>());
    registerItem(std::make_unique<ItemStick>());
    registerItem(std::make_unique<ItemFlint>());
    registerItem(std::make_unique<ItemFeather>());
    registerItem(std::make_unique<ItemLeather>());
    registerItem(std::make_unique<ItemGunpowder>());
    registerItem(std::make_unique<ItemEmerald>());
    registerItem(std::make_unique<ItemRedstone>());
    registerItem(std::make_unique<ItemSugar>());
    registerItem(std::make_unique<ItemArrow>());
    registerItem(std::make_unique<ItemSnowball>());
    registerItem(std::make_unique<ItemEgg>());
    registerItem(std::make_unique<ItemSign>());
    registerItem(std::make_unique<ItemBucket>());
    registerItem(std::make_unique<ItemWheat>());
    registerItem(std::make_unique<ItemWheatSeeds>());
    registerItem(std::make_unique<ItemSugarcane>());
    registerItem(std::make_unique<ItemBow>());
    registerItem(std::make_unique<ItemFlintSteel>());
    registerItem(std::make_unique<ItemShears>());
    // tools — pickaxes
    registerItem(std::make_unique<ItemPickaxeWooden>());
    registerItem(std::make_unique<ItemPickaxeStone>());
    registerItem(std::make_unique<ItemPickaxeIron>());
    registerItem(std::make_unique<ItemPickaxeGolden>());
    registerItem(std::make_unique<ItemPickaxeDiamond>());
    // axes
    registerItem(std::make_unique<ItemAxeWooden>());
    registerItem(std::make_unique<ItemAxeStone>());
    registerItem(std::make_unique<ItemAxeIron>());
    registerItem(std::make_unique<ItemAxeGolden>());
    registerItem(std::make_unique<ItemAxeDiamond>());
    // shovels
    registerItem(std::make_unique<ItemShovelWooden>());
    registerItem(std::make_unique<ItemShovelStone>());
    registerItem(std::make_unique<ItemShovelIron>());
    registerItem(std::make_unique<ItemShovelGolden>());
    registerItem(std::make_unique<ItemShovelDiamond>());
    // swords
    registerItem(std::make_unique<ItemSwordWooden>());
    registerItem(std::make_unique<ItemSwordStone>());
    registerItem(std::make_unique<ItemSwordIron>());
    registerItem(std::make_unique<ItemSwordGolden>());
    registerItem(std::make_unique<ItemSwordDiamond>());
    // hoes
    registerItem(std::make_unique<ItemHoeWooden>());
    registerItem(std::make_unique<ItemHoeStone>());
    registerItem(std::make_unique<ItemHoeIron>());
    registerItem(std::make_unique<ItemHoeGolden>());
    registerItem(std::make_unique<ItemHoeDiamond>());
    // armor
    registerItem(std::make_unique<ItemIronHelmet>());
    registerItem(std::make_unique<ItemIronChestplate>());
    registerItem(std::make_unique<ItemIronLeggings>());
    registerItem(std::make_unique<ItemIronBoots>());
    registerItem(std::make_unique<ItemDiamondHelmet>());
    registerItem(std::make_unique<ItemDiamondChestplate>());
    registerItem(std::make_unique<ItemDiamondLeggings>());
    registerItem(std::make_unique<ItemDiamondBoots>());
    registerItem(std::make_unique<ItemLeatherHelmet>());
    registerItem(std::make_unique<ItemLeatherChestplate>());
    registerItem(std::make_unique<ItemLeatherLeggings>());
    registerItem(std::make_unique<ItemLeatherBoots>());
    registerItem(std::make_unique<ItemGoldHelmet>());
    registerItem(std::make_unique<ItemGoldChestplate>());
    registerItem(std::make_unique<ItemGoldLeggings>());
    registerItem(std::make_unique<ItemGoldBoots>());
}
} // namespace
