/*
 * Credits: silviu20092
 */

#include "ScriptMgr.h"
#include "item_reforge.h"
#include "ScriptMgr.h"
#include "Player.h"
#include "Item.h"
#include "item_reforge.h"


class mod_reforging_itemscript : public AllItemScript
{
public:
    mod_reforging_itemscript() : AllItemScript("mod_reforging_itemscript") {}

    bool CanItemRemove(Player* player, Item* item) override
    {
        sItemReforge->RemoveReforge(player, item);
        return true;
    }
};

class ModReforging_ItemScript : public ItemScript
{
public:
    ModReforging_ItemScript() : ItemScript("ModReforging_ItemScript") {}

    // 修复后的函数签名
    bool OnApplyEquipSpell(Player* player, Item* item, SpellInfo const* spell) 
    {
        if (!ItemReforge::HasReforge(item))
            return false;

        // 动态应用重铸加成
        uint32 decrease = ItemReforge::GetReforgeDecrease(item);
        uint32 increase = ItemReforge::GetReforgeIncrease(item);
        uint32 value = ItemReforge::GetReforgeValue(item);

        // 修复后的调用
        player->HandleStatModifier(static_cast<UnitMods>(decrease), BASE_VALUE, -int32(value), true);
        player->HandleStatModifier(static_cast<UnitMods>(increase), BASE_VALUE, int32(value), true);

        return true;
    }
};

void Addmod_reforging_itemscript()
{
    new ModReforging_ItemScript();
}

void AddSC_mod_reforging_itemscript()
{
    new mod_reforging_itemscript();
}
