/*
 * Credits: silviu20092
 */

#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "Creature.h"
#include "Player.h"
#include "StringConvert.h"
#include "item_reforge.h"

class npc_reforger : public CreatureScript
{
private:
    std::unordered_map<uint32, ObjectGuid> itemMap;

    bool CloseGossip(Player* player, bool retVal = true)
    {
        CloseGossipMenuFor(player);
        return retVal;
    }

    bool AddEquipmentSlotMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        const std::vector<uint32>& reforgeableStats = sItemReforge->GetReforgeableStats();
        std::ostringstream oss;
        oss << "每次" << sItemReforge->GetNeedMoney() / 10000 << "金,可重铸成属性: ";
        bool hasStats = false;
        for (uint32 i = 0; i < reforgeableStats.size(); i++)
        {
            hasStats = true;
            oss << sItemReforge->StatTypeToString(reforgeableStats[i]);
            if (i < reforgeableStats.size() - 1)
                oss << ", ";
        }
        if (!hasStats)
            oss << ItemReforge::TextRed("无");

        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, oss.str(), GOSSIP_SENDER_MAIN + 1, EQUIPMENT_SLOT_END);

        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; slot++)
        {
            Item* item = sItemReforge->GetItemInSlot(player, slot);
            std::ostringstream oss;
            oss << sItemReforge->GetSlotIcon(slot);
            oss << sItemReforge->GetSlotName(slot);

            if (item == nullptr)
                oss << " [" << ItemReforge::TextRed("无物品") << "]";
            else
            {
                if (sItemReforge->IsAlreadyReforged(item))
                    oss << " [" << ItemReforge::TextRed("已重铸") << "]";
                else if (!sItemReforge->IsReforgeable(player, item))
                    oss << " [" << ItemReforge::TextRed("不可重铸") << "]";
                else
                    oss << " [" << ItemReforge::TextGreen("可重铸") << "]";
            }

            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, oss.str(), GOSSIP_SENDER_MAIN + 1, slot);
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "返回", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }

    bool CanAdvanceWithReforging(Player* player, const Item* item) const
    {
        if (item == nullptr)
        {
            ItemReforge::SendMessage(player, "当前槽位没有装备");
            return false;
        }
        else if (sItemReforge->IsAlreadyReforged(item))
        {
            ItemReforge::SendMessage(player, "该物品已重铸");
            return false;
        }
        else if (!sItemReforge->IsReforgeable(player, item))
        {
            ItemReforge::SendMessage(player, "该物品不能重铸");
            return false;
        }

        return true;
    }

    bool AddReforgingMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        ObjectGuid itemGuid = itemMap[player->GetGUID().GetCounter()];
        Item* item = player->GetItemByGuid(itemGuid);
        if (!CanAdvanceWithReforging(player, item))
            return CloseGossip(player, false);

        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, ItemReforge::ItemLinkForUI(item, player), GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + 100);

        std::vector<_ItemStat> itemStats = sItemReforge->LoadItemStatInfo(item, true);
        for (const _ItemStat& stat : itemStats)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "重铸 " + sItemReforge->StatTypeToString(stat.ItemStatType), GOSSIP_SENDER_MAIN + 2, stat.ItemStatType);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "返回", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }

    bool AddReforgingStatsMenu(Player* player, Creature* creature, uint32 stat)
    {
        ClearGossipMenuFor(player);

        ObjectGuid itemGuid = itemMap[player->GetGUID().GetCounter()];
        Item* item = player->GetItemByGuid(itemGuid);
        if (!CanAdvanceWithReforging(player, item))
            return CloseGossip(player, false);

        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, ItemReforge::ItemLinkForUI(item, player), GOSSIP_SENDER_MAIN + 2, stat);

        const std::vector<uint32>& reforgeableStats = sItemReforge->GetReforgeableStats();
        std::vector<_ItemStat> itemStats = sItemReforge->LoadItemStatInfo(item);
        const _ItemStat* toReforgeStat = sItemReforge->FindItemStat(itemStats, stat);
        if (toReforgeStat == nullptr)
            return CloseGossip(player, false);

        uint32 taken = sItemReforge->CalculateReforgePct(toReforgeStat->ItemStatValue);
        uint32 newVal = toReforgeStat->ItemStatValue - taken;
        std::ostringstream oss;
        oss << "将扣除 " << ItemReforge::TextRed(Acore::ToString((uint32)sItemReforge->GetPercentage()) + "% ") << sItemReforge->StatTypeToString(stat);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, oss.str(), GOSSIP_SENDER_MAIN + 2, stat);

        oss.str("");
        oss << sItemReforge->StatTypeToString(stat) << " 重铸后: ";
        oss << ItemReforge::TextRed(Acore::ToString(newVal)) << " (-" << Acore::ToString(taken) << ")";
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, oss.str(), GOSSIP_SENDER_MAIN + 2, stat);

        for (const uint32& rstat : reforgeableStats)
        {
            if (sItemReforge->FindItemStat(itemStats, rstat) != nullptr)
                continue;

            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, ItemReforge::TextGreen("+" + Acore::ToString(taken) + " " + sItemReforge->StatTypeToString(rstat)), GOSSIP_SENDER_MAIN + 10 + stat, rstat, "确定要重铸该物品?", 0, false);
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "返回", GOSSIP_SENDER_MAIN + 2, GOSSIP_ACTION_INFO_DEF + 100);

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }

    bool AddRemoveReforgeMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; slot++)
        {
            Item* item = sItemReforge->GetItemInSlot(player, slot);
            std::ostringstream oss;
            oss << sItemReforge->GetSlotIcon(slot);
            oss << sItemReforge->GetSlotName(slot);

            if (item == nullptr)
                oss << " [" << ItemReforge::TextRed("无物品") << "]";
            else
            {
                if (sItemReforge->IsAlreadyReforged(item))
                    oss << " [" << ItemReforge::TextGreen("已重铸") << "]";
                else
                    oss << " [" << ItemReforge::TextRed("未重铸") << "]";
            }

            AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, oss.str(), GOSSIP_SENDER_MAIN + 3, slot);
        }

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "返回", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }

    bool AddRemoveReforgeStatsMenu(Player* player, Creature* creature)
    {
        ClearGossipMenuFor(player);

        ObjectGuid itemGuid = itemMap[player->GetGUID().GetCounter()];
        Item* item = player->GetItemByGuid(itemGuid);
        if (!sItemReforge->CanRemoveReforge(item))
            return CloseGossip(player, false);

        AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, ItemReforge::ItemLinkForUI(item, player), GOSSIP_SENDER_MAIN + 4, GOSSIP_ACTION_INFO_DEF);

        const ItemReforge::ReforgingData* reforging = sItemReforge->GetReforgingData(item);
        if (reforging == nullptr)
            return CloseGossip(player, false);

        std::vector<_ItemStat> itemStats = sItemReforge->LoadItemStatInfo(item);
        const _ItemStat* decreasedStat = sItemReforge->FindItemStat(itemStats, reforging->stat_decrease);
        if (decreasedStat == nullptr)
            return CloseGossip(player, false);

        std::ostringstream oss;
        oss << "将恢复 " << sItemReforge->StatTypeToString(decreasedStat->ItemStatType) << " 为 " << ItemReforge::TextGreen(Acore::ToString(decreasedStat->ItemStatValue));
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, oss.str(), GOSSIP_SENDER_MAIN + 4, GOSSIP_ACTION_INFO_DEF);

        oss.str("");
        oss << ItemReforge::TextRed("-" + Acore::ToString(reforging->stat_value) + " " + sItemReforge->StatTypeToString(reforging->stat_increase));
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, oss.str(), GOSSIP_SENDER_MAIN + 4, GOSSIP_ACTION_INFO_DEF);

        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, ItemReforge::TextRed("[恢复]"), GOSSIP_SENDER_MAIN + 4, GOSSIP_ACTION_INFO_DEF + 1, "你确定吗?", 0, false);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "返回", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 3);

        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }
public:
    npc_reforger() : CreatureScript("npc_reforger") {}

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!sItemReforge->GetEnabled())
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "|cffb50505不可用|r", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
        else
        {
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "选择重铸的槽位", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "从物品移除重铸", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 3);
        }
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "再见!", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
        SendGossipMenuFor(player, DEFAULT_GOSSIP_MESSAGE, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        if (!sItemReforge->GetEnabled())
            return CloseGossip(player);

        if (sender == GOSSIP_SENDER_MAIN)
        {
            if (action == GOSSIP_ACTION_INFO_DEF)
            {
                ClearGossipMenuFor(player);
                return OnGossipHello(player, creature);
            }
            else if (action == GOSSIP_ACTION_INFO_DEF + 1)
                return AddEquipmentSlotMenu(player, creature);
            else if (action == GOSSIP_ACTION_INFO_DEF + 2)
                return CloseGossip(player);
            else if (action == GOSSIP_ACTION_INFO_DEF + 3)
                return AddRemoveReforgeMenu(player, creature);
        }
        else if (sender == GOSSIP_SENDER_MAIN + 1)
        {
            uint8 slot = (uint8)action;
            if (slot == EQUIPMENT_SLOT_END)
                return AddEquipmentSlotMenu(player, creature);

            Item* item = sItemReforge->GetItemInSlot(player, slot);
            if (!CanAdvanceWithReforging(player, item))
                return AddEquipmentSlotMenu(player, creature);
            else
            {
                itemMap[player->GetGUID().GetCounter()] = item->GetGUID();
                return AddReforgingMenu(player, creature);
            }
        }
        else if (sender == GOSSIP_SENDER_MAIN + 2)
        {
            if (action == GOSSIP_ACTION_INFO_DEF + 100)
                return AddReforgingMenu(player, creature);
            else
                return AddReforgingStatsMenu(player, creature, action);
        }
        else if (sender == GOSSIP_SENDER_MAIN + 3)
        {
            uint8 slot = (uint8)action;
            Item* item = sItemReforge->GetItemInSlot(player, slot);
            if (!sItemReforge->CanRemoveReforge(item))
                return AddRemoveReforgeMenu(player, creature);
            else
            {
                itemMap[player->GetGUID().GetCounter()] = item->GetGUID();
                return AddRemoveReforgeStatsMenu(player, creature);
            }
        }
        else if (sender == GOSSIP_SENDER_MAIN + 4)
        {
            if (action == GOSSIP_ACTION_INFO_DEF)
                return AddRemoveReforgeStatsMenu(player, creature);
            else
            {
                if (sItemReforge->RemoveReforge(player, itemMap[player->GetGUID().GetCounter()]))
                    sItemReforge->VisualFeedback(player);

                return CloseGossip(player);
            }
        }
        else if (sender >= GOSSIP_SENDER_MAIN + 10)
        {
            uint32 decreaseStat = sender - (GOSSIP_SENDER_MAIN + 10);
            uint32 increaseStat = action;
            if (!sItemReforge->Reforge(player, itemMap[player->GetGUID().GetCounter()], decreaseStat, increaseStat))
                ItemReforge::SendMessage(player, "重铸失败!请重试.");
            else
                sItemReforge->VisualFeedback(player);

            return CloseGossip(player);
        }

        return CloseGossip(player, false);
    }
};

void AddSC_npc_reforger()
{
    new npc_reforger();
}
