/*
 * Credits: silviu20092
 */

#include <cmath>
#include "DatabaseEnv.h"
#include "Player.h"
#include "Chat.h"
#include "Tokenize.h"
#include "StringConvert.h"
#include "SpellMgr.h"
#include "WorldSessionMgr.h"
#include "item_reforge.h"
#include "Item.h"
#include <unordered_map>
#include <tuple>

 /*
  * ItemReforge 实现：基于 item_instance 表的 reforge 字段实现装备实例级重铸
  * 需在数据库 item_instance 表增加字段：
  *   reforge_decrease INT DEFAULT 0,
  *   reforge_increase INT DEFAULT 0,
  *   reforge_value   INT DEFAULT 0
  */

  // 内存缓存每个 item 的重铸数据（可选加速）
static std::unordered_map<uint64, std::tuple<uint32, uint32, uint32>> s_ReforgeCache;


void ItemReforge::SetReforgeData(Item* item, uint32 decrease, uint32 increase, uint32 value)
{
    if (!item) return;
    CharacterDatabase.Execute(
        "UPDATE item_instance SET reforge_decrease = {}, reforge_increase = {}, reforge_value = {} WHERE guid = {}",
        decrease, increase, value, item->GetGUID().GetCounter()
    );
}

void ItemReforge::ClearReforgeData(Item* item)
{
    if (!item) return;
    CharacterDatabase.Execute(
        "UPDATE item_instance SET reforge_decrease = 0, reforge_increase = 0, reforge_value = 0 WHERE guid = {}",
        item->GetGUID().GetCounter()
    );
}

bool ItemReforge::HasReforge(const Item* item)
{
    if (!item) return false;
    QueryResult result = CharacterDatabase.Query(
        "SELECT reforge_decrease, reforge_increase, reforge_value FROM item_instance WHERE guid = {}",
        item->GetGUID().GetCounter()
    );
    if (!result) return false;
    Field* fields = result->Fetch();
    return fields[0].Get<uint32>() != 0 || fields[1].Get<uint32>() != 0 || fields[2].Get<uint32>() != 0;
}

uint32 ItemReforge::GetReforgeDecrease(const Item* item)
{
    if (!item) return 0;
    QueryResult result = CharacterDatabase.Query(
        "SELECT reforge_decrease FROM item_instance WHERE guid = {}",
        item->GetGUID().GetCounter()
    );
    if (!result) return 0;
    return result->Fetch()[0].Get<uint32>();
}

uint32 ItemReforge::GetReforgeIncrease(const Item* item)
{
    if (!item) return 0;
    QueryResult result = CharacterDatabase.Query(
        "SELECT reforge_increase FROM item_instance WHERE guid = {}",
        item->GetGUID().GetCounter()
    );
    if (!result) return 0;
    return result->Fetch()[0].Get<uint32>();
}

uint32 ItemReforge::GetReforgeValue(const Item* item)
{
    if (!item) return 0;
    QueryResult result = CharacterDatabase.Query(
        "SELECT reforge_value FROM item_instance WHERE guid = {}",
        item->GetGUID().GetCounter()
    );
    if (!result) return 0;
    return result->Fetch()[0].Get<uint32>();
}

void ItemReforge::SaveToDB(Item* item, uint32 decrease, uint32 increase, uint32 value)
{
    if (!item) return;
    CharacterDatabase.Execute(
        "UPDATE item_instance SET reforge_decrease = {}, reforge_increase = {}, reforge_value = {} WHERE guid = {}",
        decrease, increase, value, item->GetGUID().GetCounter()
    );
}

bool ItemReforge::LoadFromDB(const Item* item, uint32& decrease, uint32& increase, uint32& value)
{
    if (!item) return false;
    QueryResult result = CharacterDatabase.Query(
        "SELECT reforge_decrease, reforge_increase, reforge_value FROM item_instance WHERE guid = {}",
        item->GetGUID().GetCounter()
    );
    if (result)
    {
        Field* fields = result->Fetch();
        decrease = fields[0].Get<uint32>();
        increase = fields[1].Get<uint32>();
        value = fields[2].Get<uint32>();
        s_ReforgeCache[item->GetGUID().GetCounter()] = std::make_tuple(decrease, increase, value);
        return true;
    }
    decrease = increase = value = 0;
    return false;
}

ItemReforge::ItemReforge()
{
    enabled = true;
    percentage = PERCENTAGE_DEFAULT;
    NeedMoney = NEEDMONEY_DEFAULT;
}

ItemReforge::~ItemReforge() {}

/*static*/ ItemReforge* ItemReforge::instance()
{
    static ItemReforge instance;
    return &instance;
}

void ItemReforge::SetEnabled(bool value)
{
    enabled = value;
}

bool ItemReforge::GetEnabled() const
{
    return enabled;
}

void ItemReforge::SetReforgeableStats(const std::string& stats)
{
    reforgeableStats.clear();
    std::vector<std::string_view> tokenized = Acore::Tokenize(stats, ',', false);
    if (tokenized.size() <= MAX_REFORGEABLE_STATS)
    {
        std::transform(tokenized.begin(), tokenized.end(), std::back_inserter(reforgeableStats),
            [](const std::string_view& str) { return *Acore::StringTo<uint32>(str); });
    }
}

bool ItemReforge::IsReforgeableStat(uint32 stat) const
{
    std::vector<uint32>::const_iterator citer = std::find_if(reforgeableStats.begin(), reforgeableStats.end(), [&](const uint32& s) { return s == stat; });
    return citer != reforgeableStats.end();
}

const std::vector<uint32>& ItemReforge::GetReforgeableStats() const
{
    return reforgeableStats;
}

void ItemReforge::SetPercentage(float value)
{
    if (value < PERCENTAGE_MIN || value > PERCENTAGE_MAX)
        percentage = PERCENTAGE_DEFAULT;
    else
        percentage = value;
}

float ItemReforge::GetPercentage() const
{
    return percentage;
}

void ItemReforge::SetNeedMoney(uint32 value)
{
    if (value < 0)
		NeedMoney = NEEDMONEY_DEFAULT;
	else
	    NeedMoney = value;
}

uint32 ItemReforge::GetNeedMoney() const
{
    return NeedMoney;
}

void ItemReforge::CleanupDB() const
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    CharacterDatabase.DirectExecute("DELETE FROM character_reforging WHERE guid NOT IN (SELECT guid FROM characters)");
    CharacterDatabase.DirectExecute("DELETE FROM character_reforging WHERE item_guid NOT IN (SELECT guid FROM item_instance)");
    CharacterDatabase.DirectCommitTransaction(trans);
}

void ItemReforge::LoadFromDB()
{
    reforgingDataMap.clear();

    CleanupDB();

    uint32 oldMSTime = getMSTime();

    QueryResult result = CharacterDatabase.Query("SELECT guid, item_guid, stat_decrease, stat_increase, stat_value FROM character_reforging");
    if (!result)
    {
        LOG_INFO("server.loading", ">> Loaded 0 item reforges.");
        LOG_INFO("server.loading", " ");
        return;
    }

    do
    {
        Field* fields = result->Fetch();

        ReforgingData reforgingData;
        reforgingData.guid = fields[0].Get<uint32>();
        reforgingData.item_guid = fields[1].Get<uint32>();
        reforgingData.stat_decrease = fields[2].Get<uint32>();
        reforgingData.stat_increase = fields[3].Get<uint32>();
        reforgingData.stat_value = fields[4].Get<uint32>();
        reforgingDataMap[reforgingData.item_guid] = reforgingData;
    } while (result->NextRow());

    LOG_INFO("server.loading", ">> Loaded {} item reforges in {} ms", reforgingDataMap.size(), GetMSTimeDiffToNow(oldMSTime));
    LOG_INFO("server.loading", " ");
}

std::string ItemReforge::GetSlotIcon(uint8 slot, uint32 width, uint32 height, int x, int y) const
{
    std::ostringstream ss;
    ss << "|TInterface/PaperDoll/";
    switch (slot)
    {
        case EQUIPMENT_SLOT_HEAD:
            ss << "UI-PaperDoll-Slot-Head";
            break;
        case EQUIPMENT_SLOT_NECK:
            ss << "UI-PaperDoll-Slot-Neck";
            break;
        case EQUIPMENT_SLOT_SHOULDERS:
            ss << "UI-PaperDoll-Slot-Shoulder";
            break;
        case EQUIPMENT_SLOT_BODY:
            ss << "UI-PaperDoll-Slot-Shirt";
            break;
        case EQUIPMENT_SLOT_CHEST:
            ss << "UI-PaperDoll-Slot-Chest";
            break;
        case EQUIPMENT_SLOT_WAIST:
            ss << "UI-PaperDoll-Slot-Waist";
            break;
        case EQUIPMENT_SLOT_LEGS:
            ss << "UI-PaperDoll-Slot-Legs";
            break;
        case EQUIPMENT_SLOT_FEET:
            ss << "UI-PaperDoll-Slot-Feet";
            break;
        case EQUIPMENT_SLOT_WRISTS:
            ss << "UI-PaperDoll-Slot-Wrists";
            break;
        case EQUIPMENT_SLOT_HANDS:
            ss << "UI-PaperDoll-Slot-Hands";
            break;
        case EQUIPMENT_SLOT_FINGER1:
        case EQUIPMENT_SLOT_FINGER2:
            ss << "UI-PaperDoll-Slot-Finger";
            break;
        case EQUIPMENT_SLOT_TRINKET1:
        case EQUIPMENT_SLOT_TRINKET2:
            ss << "UI-PaperDoll-Slot-Trinket";
            break;     
        case EQUIPMENT_SLOT_BACK:
            ss << "UI-PaperDoll-Slot-Chest";
            break;
        case EQUIPMENT_SLOT_MAINHAND:
            ss << "UI-PaperDoll-Slot-MainHand";
            break;
        case EQUIPMENT_SLOT_OFFHAND:
            ss << "UI-PaperDoll-Slot-SecondaryHand";
            break;
        case EQUIPMENT_SLOT_RANGED:
            ss << "UI-PaperDoll-Slot-Ranged";
            break;
        case EQUIPMENT_SLOT_TABARD:
            ss << "UI-PaperDoll-Slot-Tabard";
            break;
        default:
            ss << "UI-Backpack-EmptySlot";
            break;
    }
    ss << ":" << width << ":" << height << ":" << x << ":" << y << "|t";
    return ss.str();
}

std::string ItemReforge::GetSlotName(uint8 slot) const
{
    switch (slot)
    {
        case EQUIPMENT_SLOT_HEAD:
            return "头部";
        case EQUIPMENT_SLOT_NECK:
            return "颈部";
        case EQUIPMENT_SLOT_SHOULDERS:
            return "肩膀";
        case EQUIPMENT_SLOT_BODY:
            return "衬衣";
        case EQUIPMENT_SLOT_CHEST:
            return "胸部";
        case EQUIPMENT_SLOT_WAIST:
            return "腰部";
        case EQUIPMENT_SLOT_LEGS:
            return "腿部";
        case EQUIPMENT_SLOT_FEET:
            return "脚";
        case EQUIPMENT_SLOT_WRISTS:
            return "手腕";
        case EQUIPMENT_SLOT_HANDS:
            return "手";
        case EQUIPMENT_SLOT_FINGER1:
            return "手指一";
        case EQUIPMENT_SLOT_FINGER2:
            return "手指二";
        case EQUIPMENT_SLOT_TRINKET1:
            return "饰品一";
        case EQUIPMENT_SLOT_TRINKET2:
            return "饰品二";
        case EQUIPMENT_SLOT_BACK:
            return "披风";
        case EQUIPMENT_SLOT_MAINHAND:
            return "主手";
        case EQUIPMENT_SLOT_OFFHAND:
            return "副手";
        case EQUIPMENT_SLOT_RANGED:
            return "远程";
        case EQUIPMENT_SLOT_TABARD:
            return "战袍";
        default:
            return "未知";
    }
}

std::string ItemReforge::StatTypeToString(uint32 statType) const
{
    static std::unordered_map<uint32, std::string> statTypeToStrMap = {
        {ITEM_MOD_MANA, "法力"}, {ITEM_MOD_HEALTH, "治疗"}, {ITEM_MOD_AGILITY, "敏捷"},
        {ITEM_MOD_STRENGTH, "力量"}, {ITEM_MOD_INTELLECT, "智力"}, {ITEM_MOD_SPIRIT, "精神"},
        {ITEM_MOD_STAMINA, "耐力"}, {ITEM_MOD_DEFENSE_SKILL_RATING, "防御"}, {ITEM_MOD_DODGE_RATING, "躲闪"},
        {ITEM_MOD_PARRY_RATING, "招架"}, {ITEM_MOD_BLOCK_RATING, "格挡"}, {ITEM_MOD_HIT_MELEE_RATING, "近战命中"},
        {ITEM_MOD_HIT_RANGED_RATING, "远程命中"}, {ITEM_MOD_HIT_SPELL_RATING, "技能命中"}, {ITEM_MOD_CRIT_MELEE_RATING, "近战暴击"},
        {ITEM_MOD_CRIT_RANGED_RATING, "远程暴击"}, {ITEM_MOD_CRIT_SPELL_RATING, "法术暴击"}, {ITEM_MOD_HIT_TAKEN_MELEE_RATING, "近战被命中"},
        {ITEM_MOD_HIT_TAKEN_RANGED_RATING, "远程被命中"}, {ITEM_MOD_HIT_TAKEN_SPELL_RATING, "法术被命中"}, {ITEM_MOD_CRIT_TAKEN_MELEE_RATING, "近战被暴击"},
        {ITEM_MOD_CRIT_TAKEN_RANGED_RATING, "远程被暴击"}, {ITEM_MOD_CRIT_TAKEN_SPELL_RATING, "法术被暴击"}, {ITEM_MOD_HASTE_MELEE_RATING, "近战急速"},
        {ITEM_MOD_HASTE_RANGED_RATING, "远程急速"}, {ITEM_MOD_HASTE_SPELL_RATING, "法术急速"}, {ITEM_MOD_HIT_RATING, "命中"},
        {ITEM_MOD_CRIT_RATING, "暴击"}, {ITEM_MOD_HIT_TAKEN_RATING, "被命中"}, {ITEM_MOD_CRIT_TAKEN_RATING, "被暴击"},
        {ITEM_MOD_RESILIENCE_RATING, "韧性"}, {ITEM_MOD_HASTE_RATING, "急速"}, {ITEM_MOD_EXPERTISE_RATING, "精准"},
        {ITEM_MOD_ATTACK_POWER, "近战攻强"}, {ITEM_MOD_RANGED_ATTACK_POWER, "远程攻强"}, {ITEM_MOD_MANA_REGENERATION, "法力恢复"},
        {ITEM_MOD_ARMOR_PENETRATION_RATING, "护甲穿透"}, {ITEM_MOD_SPELL_POWER, "法术强度"}, {ITEM_MOD_HEALTH_REGEN, "生命恢复"},
        {ITEM_MOD_SPELL_PENETRATION, "法术穿透"}, {ITEM_MOD_BLOCK_VALUE, "格挡值"}
    };

    if (statTypeToStrMap.find(statType) != statTypeToStrMap.end())
        return statTypeToStrMap.at(statType);

    return "未知";
}

bool ItemReforge::IsReforgeable(const Player* player, const Item* item) const
{
    if (!item || !item->IsEquipped())
        return false;

    if (item->GetOwnerGUID() != player->GetGUID())
        return false;

    if (reforgeableStats.empty())
        return false;

    const ItemTemplate* proto = item->GetTemplate();
    if (!proto->StatsCount || proto->StatsCount >= MAX_ITEM_PROTO_STATS)
        return false;

    if (proto->Quality > ITEM_QUALITY_LEGENDARY)
        return false;

    if (IsAlreadyReforged(item))
        return false;

    for (uint32 i = 0; i < proto->StatsCount; i++)
    {
        if (!IsReforgeableStat(proto->ItemStat[i].ItemStatType))
            continue;
        if (CalculateReforgePct(proto->ItemStat[i].ItemStatValue) >= 1)
            return true;
    }

    return false;
}

bool ItemReforge::IsAlreadyReforged(const Item* item) const
{
    return reforgingDataMap.find(item->GetGUID().GetCounter()) != reforgingDataMap.end();
}

Item* ItemReforge::GetItemInSlot(const Player* player, uint8 slot) const
{
    return player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
}

uint32 ItemReforge::CalculateReforgePct(int32 value) const
{
    if (value <= 0)
        return 0;

    return (uint32)(std::floor((float)value * (GetPercentage() / 100.0f)));
}

std::vector<_ItemStat> ItemReforge::LoadItemStatInfo(const Item* item, bool onlyReforgeable) const
{
    std::vector<_ItemStat> statInfo;
    ItemTemplate const* proto = item->GetTemplate();

    for (uint8 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
    {
        if (i >= proto->StatsCount)
            continue;

        uint32 statType = proto->ItemStat[i].ItemStatType;
        int32 statValue = proto->ItemStat[i].ItemStatValue;
        if (statValue <= 0)
            continue;

        if (onlyReforgeable && !IsReforgeableStat(statType))
            continue;

        _ItemStat stat;
        stat.ItemStatType = statType;
        stat.ItemStatValue = statValue;
        statInfo.push_back(stat);
    }

    return statInfo;
}

const _ItemStat* ItemReforge::FindItemStat(const std::vector<_ItemStat>& stats, uint32 statType) const
{
    std::vector<_ItemStat>::const_iterator citer = std::find_if(stats.begin(), stats.end(), [&](const _ItemStat& stat) { return stat.ItemStatType == statType; });
    if (citer != stats.end())
        return &*citer;

    return nullptr;
}

bool ItemReforge::Reforge(Player* player, ObjectGuid itemGuid, uint32 statDecrease, uint32 statIncrease)
{
    Item* item = player->GetItemByGuid(itemGuid);
    if (!IsReforgeable(player, item))
        return false;

    std::vector<_ItemStat> itemStats = LoadItemStatInfo(item);
    const _ItemStat* decreasedStat = FindItemStat(itemStats, statDecrease);
    if (decreasedStat == nullptr)
        return false;

    if (FindItemStat(itemStats, statIncrease) != nullptr)
        return false;

    if (!player->HasEnoughMoney(GetNeedMoney()))
    {
        ItemReforge::SendMessage(player, "你没有足够的钱重铸");
        return false;
	}

    player->_ApplyItemMods(item, item->GetSlot(), false);

    uint32 value = CalculateReforgePct(decreasedStat->ItemStatValue);
    ReforgingData reforgingData;
    reforgingData.guid = player->GetGUID().GetCounter();
    reforgingData.item_guid = item->GetGUID().GetCounter();
    reforgingData.stat_decrease = statDecrease;
    reforgingData.stat_increase = statIncrease;
    reforgingData.stat_value = value;
    reforgingDataMap[reforgingData.item_guid] = reforgingData;

    player->_ApplyItemMods(item, item->GetSlot(), true);

    CharacterDatabase.Execute("INSERT INTO character_reforging (guid, item_guid, stat_decrease, stat_increase, stat_value) VALUES ({}, {}, {}, {}, {})",
        reforgingData.guid, reforgingData.item_guid, statDecrease, statIncrease, value);

    SendItemPacket(player, item);
    player->ModifyMoney(- GetNeedMoney());
    return true;
}

const ItemReforge::ReforgingData* ItemReforge::GetReforgingData(const Item* item) const
{
    if (!GetEnabled())
        return nullptr;

    if (IsAlreadyReforged(item))
        return &reforgingDataMap.at(item->GetGUID().GetCounter());

    return nullptr;
}

std::vector<Item*> ItemReforge::GetPlayerItems(const Player* player, bool inBankAlso) const
{
    std::vector<Item*> items;
    for (uint8 i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; i++)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            items.push_back(item);

    for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
        if (Bag* bag = player->GetBagByPos(i))
            for (uint32 j = 0; j < bag->GetBagSize(); j++)
                if (Item* item = player->GetItemByPos(i, j))
                    items.push_back(item);

    for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; i++)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            items.push_back(item);

    if (inBankAlso)
    {
        for (uint8 i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; i++)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                items.push_back(item);

        for (uint8 i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++)
            if (Bag* bag = player->GetBagByPos(i))
                for (uint32 j = 0; j < bag->GetBagSize(); j++)
                    if (Item* item = player->GetItemByPos(i, j))
                        items.push_back(item);
    }

    return items;
}

bool ItemReforge::CanRemoveReforge(const Item* item) const
{
    if (!item || !item->IsEquipped())
        return false;

    return IsAlreadyReforged(item);
}

bool ItemReforge::RemoveReforge(Player* player, ObjectGuid itemGuid)
{
    return RemoveReforge(player, player->GetItemByGuid(itemGuid));
}

bool ItemReforge::RemoveReforge(Player* player, Item* item)
{
    if (!item || !IsAlreadyReforged(item))
        return false;

    bool equipped = item->IsEquipped();

    if (equipped)
        player->_ApplyItemMods(item, item->GetSlot(), false);

    reforgingDataMap.erase(item->GetGUID().GetCounter());

    if (equipped)
        player->_ApplyItemMods(item, item->GetSlot(), true);
    
    CharacterDatabase.Execute("DELETE FROM character_reforging WHERE item_guid = {}", item->GetGUID().GetCounter());

    SendItemPacket(player, item);

    return true;
}

void ItemReforge::VisualFeedback(Player* player)
{
    player->CastSpell(player, VISUAL_FEEDBACK_SPELL_ID, true);
}

void ItemReforge::HandleCharacterRemove(uint32 guid)
{
    for (auto it = reforgingDataMap.begin(); it != reforgingDataMap.end(); )
    {
        if (it->second.guid == guid)
            it = reforgingDataMap.erase(it);
        else
            ++it;
    }
}

void ItemReforge::HandleStatModifier(Player* player, uint32 statType, int32 val, bool apply)
{
    if (val == 0)
        return;

    switch (statType)
    {
        case ITEM_MOD_MANA:
            player->HandleStatModifier(UNIT_MOD_MANA, BASE_VALUE, float(val), apply);
            break;
        case ITEM_MOD_HEALTH:                           // modify HP
            player->HandleStatModifier(UNIT_MOD_HEALTH, BASE_VALUE, float(val), apply);
            break;
        case ITEM_MOD_AGILITY:                          // modify agility
            player->HandleStatModifier(UNIT_MOD_STAT_AGILITY, BASE_VALUE, float(val), apply);
            player->ApplyStatBuffMod(STAT_AGILITY, float(val), apply);
            break;
        case ITEM_MOD_STRENGTH:                         //modify strength
            player->HandleStatModifier(UNIT_MOD_STAT_STRENGTH, BASE_VALUE, float(val), apply);
            player->ApplyStatBuffMod(STAT_STRENGTH, float(val), apply);
            break;
        case ITEM_MOD_INTELLECT:                        //modify intellect
            player->HandleStatModifier(UNIT_MOD_STAT_INTELLECT, BASE_VALUE, float(val), apply);
            player->ApplyStatBuffMod(STAT_INTELLECT, float(val), apply);
            break;
        case ITEM_MOD_SPIRIT:                           //modify spirit
            player->HandleStatModifier(UNIT_MOD_STAT_SPIRIT, BASE_VALUE, float(val), apply);
            player->ApplyStatBuffMod(STAT_SPIRIT, float(val), apply);
            break;
        case ITEM_MOD_STAMINA:                          //modify stamina
            player->HandleStatModifier(UNIT_MOD_STAT_STAMINA, BASE_VALUE, float(val), apply);
            player->ApplyStatBuffMod(STAT_STAMINA, float(val), apply);
            break;
        case ITEM_MOD_DEFENSE_SKILL_RATING:
            player->ApplyRatingMod(CR_DEFENSE_SKILL, int32(val), apply);
            break;
        case ITEM_MOD_DODGE_RATING:
            player->ApplyRatingMod(CR_DODGE, int32(val), apply);
            break;
        case ITEM_MOD_PARRY_RATING:
            player->ApplyRatingMod(CR_PARRY, int32(val), apply);
            break;
        case ITEM_MOD_BLOCK_RATING:
            player->ApplyRatingMod(CR_BLOCK, int32(val), apply);
            break;
        case ITEM_MOD_HIT_MELEE_RATING:
            player->ApplyRatingMod(CR_HIT_MELEE, int32(val), apply);
            break;
        case ITEM_MOD_HIT_RANGED_RATING:
            player->ApplyRatingMod(CR_HIT_RANGED, int32(val), apply);
            break;
        case ITEM_MOD_HIT_SPELL_RATING:
            player->ApplyRatingMod(CR_HIT_SPELL, int32(val), apply);
            break;
        case ITEM_MOD_CRIT_MELEE_RATING:
            player->ApplyRatingMod(CR_CRIT_MELEE, int32(val), apply);
            break;
        case ITEM_MOD_CRIT_RANGED_RATING:
            player->ApplyRatingMod(CR_CRIT_RANGED, int32(val), apply);
            break;
        case ITEM_MOD_CRIT_SPELL_RATING:
            player->ApplyRatingMod(CR_CRIT_SPELL, int32(val), apply);
            break;
        case ITEM_MOD_HIT_TAKEN_MELEE_RATING:
            player->ApplyRatingMod(CR_HIT_TAKEN_MELEE, int32(val), apply);
            break;
        case ITEM_MOD_HIT_TAKEN_RANGED_RATING:
            player->ApplyRatingMod(CR_HIT_TAKEN_RANGED, int32(val), apply);
            break;
        case ITEM_MOD_HIT_TAKEN_SPELL_RATING:
            player->ApplyRatingMod(CR_HIT_TAKEN_SPELL, int32(val), apply);
            break;
        case ITEM_MOD_CRIT_TAKEN_MELEE_RATING:
            player->ApplyRatingMod(CR_CRIT_TAKEN_MELEE, int32(val), apply);
            break;
        case ITEM_MOD_CRIT_TAKEN_RANGED_RATING:
            player->ApplyRatingMod(CR_CRIT_TAKEN_RANGED, int32(val), apply);
            break;
        case ITEM_MOD_CRIT_TAKEN_SPELL_RATING:
            player->ApplyRatingMod(CR_CRIT_TAKEN_SPELL, int32(val), apply);
            break;
        case ITEM_MOD_HASTE_MELEE_RATING:
            player->ApplyRatingMod(CR_HASTE_MELEE, int32(val), apply);
            break;
        case ITEM_MOD_HASTE_RANGED_RATING:
            player->ApplyRatingMod(CR_HASTE_RANGED, int32(val), apply);
            break;
        case ITEM_MOD_HASTE_SPELL_RATING:
            player->ApplyRatingMod(CR_HASTE_SPELL, int32(val), apply);
            break;
        case ITEM_MOD_HIT_RATING:
            player->ApplyRatingMod(CR_HIT_MELEE, int32(val), apply);
            player->ApplyRatingMod(CR_HIT_RANGED, int32(val), apply);
            player->ApplyRatingMod(CR_HIT_SPELL, int32(val), apply);
            break;
        case ITEM_MOD_CRIT_RATING:
            player->ApplyRatingMod(CR_CRIT_MELEE, int32(val), apply);
            player->ApplyRatingMod(CR_CRIT_RANGED, int32(val), apply);
            player->ApplyRatingMod(CR_CRIT_SPELL, int32(val), apply);
            break;
        case ITEM_MOD_HIT_TAKEN_RATING:
            player->ApplyRatingMod(CR_HIT_TAKEN_MELEE, int32(val), apply);
            player->ApplyRatingMod(CR_HIT_TAKEN_RANGED, int32(val), apply);
            player->ApplyRatingMod(CR_HIT_TAKEN_SPELL, int32(val), apply);
            break;
        case ITEM_MOD_CRIT_TAKEN_RATING:
        case ITEM_MOD_RESILIENCE_RATING:
            player->ApplyRatingMod(CR_CRIT_TAKEN_MELEE, int32(val), apply);
            player->ApplyRatingMod(CR_CRIT_TAKEN_RANGED, int32(val), apply);
            player->ApplyRatingMod(CR_CRIT_TAKEN_SPELL, int32(val), apply);
            break;
        case ITEM_MOD_HASTE_RATING:
            player->ApplyRatingMod(CR_HASTE_MELEE, int32(val), apply);
            player->ApplyRatingMod(CR_HASTE_RANGED, int32(val), apply);
            player->ApplyRatingMod(CR_HASTE_SPELL, int32(val), apply);
            break;
        case ITEM_MOD_EXPERTISE_RATING:
            player->ApplyRatingMod(CR_EXPERTISE, int32(val), apply);
            break;
        case ITEM_MOD_ATTACK_POWER:
            player->HandleStatModifier(UNIT_MOD_ATTACK_POWER, TOTAL_VALUE, float(val), apply);
            player->HandleStatModifier(UNIT_MOD_ATTACK_POWER_RANGED, TOTAL_VALUE, float(val), apply);
            break;
        case ITEM_MOD_RANGED_ATTACK_POWER:
            player->HandleStatModifier(UNIT_MOD_ATTACK_POWER_RANGED, TOTAL_VALUE, float(val), apply);
            break;
            //            case ITEM_MOD_FERAL_ATTACK_POWER:
            //                ApplyFeralAPBonus(int32(val), apply);
            //                break;
        case ITEM_MOD_MANA_REGENERATION:
            player->ApplyManaRegenBonus(int32(val), apply);
            break;
        case ITEM_MOD_ARMOR_PENETRATION_RATING:
            player->ApplyRatingMod(CR_ARMOR_PENETRATION, int32(val), apply);
            break;
        case ITEM_MOD_SPELL_POWER:
            player->ApplySpellPowerBonus(int32(val), apply);
            break;
        case ITEM_MOD_HEALTH_REGEN:
            player->ApplyHealthRegenBonus(int32(val), apply);
            break;
        case ITEM_MOD_SPELL_PENETRATION:
            player->ApplySpellPenetrationBonus(val, apply);
            break;
        case ITEM_MOD_BLOCK_VALUE:
            player->HandleBaseModValue(SHIELD_BLOCK_VALUE, FLAT_MOD, float(val), apply);
            break;
            /// @deprecated item mods
        case ITEM_MOD_SPELL_HEALING_DONE:
        case ITEM_MOD_SPELL_DAMAGE_DONE:
            break;
    }
}

void ItemReforge::SendItemPacket(Player* player, const Item* item) const
{
    ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(item->GetEntry());
    std::string Name = pProto->Name1;
    std::string Description = pProto->Description;

    int loc_idx = player->GetSession()->GetSessionDbLocaleIndex();
    if (loc_idx >= 0)
    {
        if (ItemLocale const* il = sObjectMgr->GetItemLocale(pProto->ItemId))
        {
            ObjectMgr::GetLocaleString(il->Name, loc_idx, Name);
            ObjectMgr::GetLocaleString(il->Description, loc_idx, Description);
        }
    }
    // guess size
    WorldPacket queryData(SMSG_ITEM_QUERY_SINGLE_RESPONSE, 600);
    queryData << pProto->ItemId;
    queryData << pProto->Class;
    queryData << pProto->SubClass;
    queryData << pProto->SoundOverrideSubclass;
    queryData << Name;
    queryData << uint8(0x00);                                //pProto->Name2; // blizz not send name there, just uint8(0x00); <-- \0 = empty string = empty name...
    queryData << uint8(0x00);                                //pProto->Name3; // blizz not send name there, just uint8(0x00);
    queryData << uint8(0x00);                                //pProto->Name4; // blizz not send name there, just uint8(0x00);
    queryData << pProto->DisplayInfoID;
    queryData << pProto->Quality;
    queryData << pProto->Flags;
    queryData << pProto->Flags2;
    queryData << pProto->BuyPrice;
    queryData << pProto->SellPrice;
    queryData << pProto->InventoryType;
    queryData << pProto->AllowableClass;
    queryData << pProto->AllowableRace;
    queryData << pProto->ItemLevel;
    queryData << pProto->RequiredLevel;
    queryData << pProto->RequiredSkill;
    queryData << pProto->RequiredSkillRank;
    queryData << pProto->RequiredSpell;
    queryData << pProto->RequiredHonorRank;
    queryData << pProto->RequiredCityRank;
    queryData << pProto->RequiredReputationFaction;
    queryData << pProto->RequiredReputationRank;
    queryData << int32(pProto->MaxCount);
    queryData << int32(pProto->Stackable);
    queryData << pProto->ContainerSlots;
    const ReforgingData* reforgingData = GetReforgingData(item);
    if (reforgingData == nullptr)
    {
        queryData << pProto->StatsCount;
        for (uint32 i = 0; i < pProto->StatsCount; ++i)
        {
            queryData << pProto->ItemStat[i].ItemStatType;
            queryData << pProto->ItemStat[i].ItemStatValue;
        }
    }
    else
    {
        queryData << pProto->StatsCount + 1;
        for (uint32 i = 0; i < pProto->StatsCount; ++i)
        {
            uint32 statType = pProto->ItemStat[i].ItemStatType;
            queryData << statType;
            if (reforgingData->stat_decrease == statType)
                queryData << pProto->ItemStat[i].ItemStatValue - reforgingData->stat_value;
            else
                queryData << pProto->ItemStat[i].ItemStatValue;
        }

        queryData << reforgingData->stat_increase;
        queryData << (int32)reforgingData->stat_value;
    }

    queryData << pProto->ScalingStatDistribution;            // scaling stats distribution
    queryData << pProto->ScalingStatValue;                   // some kind of flags used to determine stat values column
    for (int i = 0; i < MAX_ITEM_PROTO_DAMAGES; ++i)
    {
        queryData << pProto->Damage[i].DamageMin;
        queryData << pProto->Damage[i].DamageMax;
        queryData << pProto->Damage[i].DamageType;
    }

    // resistances (7)
    queryData << pProto->Armor;
    queryData << pProto->HolyRes;
    queryData << pProto->FireRes;
    queryData << pProto->NatureRes;
    queryData << pProto->FrostRes;
    queryData << pProto->ShadowRes;
    queryData << pProto->ArcaneRes;

    queryData << pProto->Delay;
    queryData << pProto->AmmoType;
    queryData << pProto->RangedModRange;

    for (int s = 0; s < MAX_ITEM_PROTO_SPELLS; ++s)
    {
        // send DBC data for cooldowns in same way as it used in Spell::SendSpellCooldown
        // use `item_template` or if not set then only use spell cooldowns
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(pProto->Spells[s].SpellId);
        if (spell)
        {
            bool db_data = pProto->Spells[s].SpellCooldown >= 0 || pProto->Spells[s].SpellCategoryCooldown >= 0;

            queryData << pProto->Spells[s].SpellId;
            queryData << pProto->Spells[s].SpellTrigger;
            queryData << int32(pProto->Spells[s].SpellCharges);

            if (db_data)
            {
                queryData << uint32(pProto->Spells[s].SpellCooldown);
                queryData << uint32(pProto->Spells[s].SpellCategory);
                queryData << uint32(pProto->Spells[s].SpellCategoryCooldown);
            }
            else
            {
                queryData << uint32(spell->RecoveryTime);
                queryData << uint32(spell->GetCategory());
                queryData << uint32(spell->CategoryRecoveryTime);
            }
        }
        else
        {
            queryData << uint32(0);
            queryData << uint32(0);
            queryData << uint32(0);
            queryData << uint32(-1);
            queryData << uint32(0);
            queryData << uint32(-1);
        }
    }
    queryData << pProto->Bonding;
    queryData << Description;
    queryData << pProto->PageText;
    queryData << pProto->LanguageID;
    queryData << pProto->PageMaterial;
    queryData << pProto->StartQuest;
    queryData << pProto->LockID;
    queryData << int32(pProto->Material);
    queryData << pProto->Sheath;
    queryData << pProto->RandomProperty;
    queryData << pProto->RandomSuffix;
    queryData << pProto->Block;
    queryData << pProto->ItemSet;
    queryData << pProto->MaxDurability;
    queryData << pProto->Area;
    queryData << pProto->Map;                                // Added in 1.12.x & 2.0.1 client branch
    queryData << pProto->BagFamily;
    queryData << pProto->TotemCategory;
    for (int s = 0; s < MAX_ITEM_PROTO_SOCKETS; ++s)
    {
        queryData << pProto->Socket[s].Color;
        queryData << pProto->Socket[s].Content;
    }
    queryData << pProto->socketBonus;
    queryData << pProto->GemProperties;
    queryData << pProto->RequiredDisenchantSkill;
    queryData << pProto->ArmorDamageModifier;
    queryData << pProto->Duration;                           // added in 2.4.2.8209, duration (seconds)
    queryData << pProto->ItemLimitCategory;                  // WotLK, ItemLimitCategory
    queryData << pProto->HolidayId;                          // Holiday.dbc?
    player->GetSession()->SendPacket(&queryData);
}

void ItemReforge::SendItemPackets(Player* player) const
{
    std::vector<Item*> items = GetPlayerItems(player, true);
    std::vector<Item*>::const_iterator itr = items.begin();
    for (/* itr */; itr != items.end(); ++itr)
        SendItemPacket(player, *itr);
}

void ItemReforge::HandleReload(Player* player, bool apply) const
{
    std::vector<Item*> playerItems = GetPlayerItems(player, true);
    std::vector<Item*>::iterator iter = playerItems.begin();
    for (/* itr */; iter != playerItems.end(); ++iter)
    {
        Item* item = *iter;
        if (apply)
            SendItemPacket(player, item);

        if (!item->IsEquipped())
            continue;

        player->_ApplyItemMods(item, item->GetSlot(), apply);
    }
}

void ItemReforge::HandleReload(bool apply) const
{
    const WorldSessionMgr::SessionMap& sessions = sWorldSessionMgr->GetAllSessions();
    WorldSessionMgr::SessionMap::const_iterator itr;
    for (itr = sessions.begin(); itr != sessions.end(); ++itr)
        if (itr->second && itr->second->GetPlayer() && itr->second->GetPlayer()->IsInWorld())
            HandleReload(itr->second->GetPlayer(), apply);
}

/*static*/ void ItemReforge::SendMessage(Player* player, const std::string& message)
{
    ChatHandler(player->GetSession()).SendSysMessage(message);
}

/*static*/ std::string ItemReforge::TextWithColor(const std::string& text, const std::string& color)
{
    return "|cff" + color + text + "|r";
}

/*static*/ std::string ItemReforge::TextRed(const std::string& text)
{
    return TextWithColor(text, RED_COLOR);
}

/*static*/ std::string ItemReforge::TextGreen(const std::string& text)
{
    return TextWithColor(text, GREEN_COLOR);
}

/*static*/ std::string ItemReforge::ItemIcon(const ItemTemplate* proto, uint32 width, uint32 height, int x, int y)
{
    std::ostringstream ss;
    ss << "|TInterface";
    const ItemDisplayInfoEntry* dispInfo = nullptr;
    if (proto)
    {
        dispInfo = sItemDisplayInfoStore.LookupEntry(proto->DisplayInfoID);
        if (dispInfo)
            ss << "/ICONS/" << dispInfo->inventoryIcon;
    }
    if (!dispInfo)
        ss << "/InventoryItems/WoWUnknownItem01";
    ss << ":" << width << ":" << height << ":" << x << ":" << y << "|t";
    return ss.str();
}

/*static*/ std::string ItemReforge::ItemNameWithLocale(const Player* player, const ItemTemplate* itemTemplate, int32 randomPropertyId)
{
    LocaleConstant loc_idx = player->GetSession()->GetSessionDbLocaleIndex();
    std::string name = itemTemplate->Name1;
    if (ItemLocale const* il = sObjectMgr->GetItemLocale(itemTemplate->ItemId))
        ObjectMgr::GetLocaleString(il->Name, loc_idx, name);

    std::array<char const*, 16> const* suffix = nullptr;
    if (randomPropertyId < 0)
    {
        if (const ItemRandomSuffixEntry* itemRandEntry = sItemRandomSuffixStore.LookupEntry(-randomPropertyId))
            suffix = &itemRandEntry->Name;
    }
    else
    {
        if (const ItemRandomPropertiesEntry* itemRandEntry = sItemRandomPropertiesStore.LookupEntry(randomPropertyId))
            suffix = &itemRandEntry->Name;
    }
    if (suffix)
    {
        std::string_view test((*suffix)[(name != itemTemplate->Name1) ? loc_idx : DEFAULT_LOCALE]);
        if (!test.empty())
        {
            name += ' ';
            name += test;
        }
    }

    return name;
}

/*static*/ std::string ItemReforge::ItemLink(const Player* player, const ItemTemplate* itemTemplate, int32 randomPropertyId)
{
    std::stringstream oss;
    oss << "|c";
    oss << std::hex << ItemQualityColors[itemTemplate->Quality] << std::dec;
    oss << "|Hitem:";
    oss << itemTemplate->ItemId;
    oss << ":0:0:0:0:0:0:0:0:0|h[";
    oss << ItemNameWithLocale(player, itemTemplate, randomPropertyId);
    oss << "]|h|r";

    return oss.str();
}

/*static*/ std::string ItemReforge::ItemLinkForUI(const Item* item, const Player* player)
{
    const ItemTemplate* proto = item->GetTemplate();
    std::ostringstream oss;
    oss << ItemIcon(proto);
    oss << ItemLink(player, proto, item->GetItemRandomPropertyId());
    return oss.str();
}
