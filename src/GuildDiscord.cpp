/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "ConfigValueCache.h"
#include "ScriptMgr.h"

enum class GuildDiscordConfig
{
    ENABLED,

    NUM_CONFIGS,
};

class GuildDiscordConfigData : public ConfigValueCache<GuildDiscordConfig>
{
public:
    GuildDiscordConfigData() : ConfigValueCache(GuildDiscordConfig::NUM_CONFIGS) { };

    void BuildConfigCache() override
    {
        SetConfigValue<bool>(GuildDiscordConfig::ENABLED, "GuildDiscord.Enable", false);
    }
};

static GuildDiscordConfigData guildDiscordConfig;

class GuildDiscordWorldScript : public WorldScript
{
public:
    GuildDiscordWorldScript() : WorldScript("GuildDiscordWorldScript", {
        WORLDHOOK_ON_BEFORE_CONFIG_LOAD
    }) { }

    void OnBeforeConfigLoad(bool reload) override
    {
        guildDiscordConfig.Initialize(reload);
    }
};

void AddGuildDiscordScripts()
{
    new GuildDiscordWorldScript();
}
