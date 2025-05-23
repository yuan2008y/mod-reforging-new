/*
 * Credits: silviu20092
 */

#include "ScriptMgr.h"
#include "Config.h"
#include "item_reforge.h"

class mod_reforging_worldscript : public WorldScript
{
public:
    mod_reforging_worldscript() : WorldScript("mod_reforging_worldscript",
        {
            WORLDHOOK_ON_AFTER_CONFIG_LOAD,
            WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED
        }) {}

    void OnAfterConfigLoad(bool reload) override
    {
        bool reforgeEnableChanged = reload && sItemReforge->GetEnabled() != sConfigMgr->GetOption<bool>("Reforging.Enable", true);
        if (reforgeEnableChanged)
            sItemReforge->HandleReload(false);

        sItemReforge->SetEnabled(sConfigMgr->GetOption<bool>("Reforging.Enable", true));
        sItemReforge->SetReforgeableStats(sConfigMgr->GetOption<std::string>("Reforging.ReforgeableStats", ItemReforge::DefaultReforgeableStats));
        sItemReforge->SetPercentage(sConfigMgr->GetOption<float>("Reforging.Percentage", ItemReforge::PERCENTAGE_DEFAULT));
        sItemReforge->SetNeedMoney(sConfigMgr->GetOption<uint32>("Reforging.NeedMoney", ItemReforge::NEEDMONEY_DEFAULT));

        if (reforgeEnableChanged)
            sItemReforge->HandleReload(true);
    }

    void OnBeforeWorldInitialized() override
    {
        sItemReforge->LoadFromDB();
    }
};

void AddSC_mod_reforging_worldscript()
{
    new mod_reforging_worldscript();
}
