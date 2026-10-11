/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "ConfigValueCache.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "RelayQueue.h"
#include "RelayText.h"
#include "ScriptMgr.h"
#include "WebhookClient.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>

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

namespace
{
    void LoadGuildTargets();
    void StartRelayWorker();
    void StopRelayWorker();
}

class GuildDiscordWorldScript : public WorldScript
{
public:
    GuildDiscordWorldScript() : WorldScript("GuildDiscordWorldScript", {
        WORLDHOOK_ON_BEFORE_CONFIG_LOAD,
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_STARTUP,
        WORLDHOOK_ON_SHUTDOWN
    }) { }

    void OnBeforeConfigLoad(bool reload) override
    {
        guildDiscordConfig.Initialize(reload);
    }

    // The initial load happens before the databases are up, so only reloads re-read the mappings here.
    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            LoadGuildTargets();
    }

    void OnStartup() override
    {
        LoadGuildTargets();
        StartRelayWorker();
    }

    void OnShutdown() override
    {
        StopRelayWorker();
    }
};

namespace
{
    constexpr std::size_t RELAY_QUEUE_CAPACITY = 1000;
    constexpr int MAX_ATTEMPTS = 3;

    struct RelayMessage
    {
        GuildDiscord::WebhookTarget target;
        uint32 guildId;
        std::string payload;
    };

    GuildDiscord::RelayQueue<RelayMessage> relayQueue(RELAY_QUEUE_CAPACITY);
    std::thread relayWorker;
    std::atomic<bool> relayStop{ false };

    // Only touched on the world thread (startup, config reload and chat hook).
    std::unordered_map<uint32, GuildDiscord::WebhookTarget> guildTargets;

    void LoadGuildTargets()
    {
        guildTargets.clear();

        QueryResult result = CharacterDatabase.Query("SELECT `guild_id`, `webhook_url` FROM `mod_discord_guild_map`");
        if (!result)
            return;

        do
        {
            Field* fields = result->Fetch();
            uint32 const guildId = fields[0].Get<uint32>();

            auto target = GuildDiscord::ParseWebhookUrl(fields[1].Get<std::string>());
            if (!target)
            {
                LOG_ERROR("module.guilddiscord", "Ignoring mod_discord_guild_map row for guild {}: not a valid Discord webhook URL.", guildId);
                continue;
            }

            guildTargets.emplace(guildId, std::move(*target));
        } while (result->NextRow());

        LOG_INFO("module.guilddiscord", "Loaded {} guild webhook mapping(s).", guildTargets.size());
    }

    // Returns false if the worker is shutting down.
    bool SleepInterruptibly(uint32 ms)
    {
        auto const end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end)
        {
            if (relayStop)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return !relayStop;
    }

    void Deliver(RelayMessage const& msg)
    {
        for (int attempt = 1; attempt <= MAX_ATTEMPTS && !relayStop; ++attempt)
        {
            GuildDiscord::PostResult const res = GuildDiscord::PostWebhook(msg.target, msg.payload);
            uint32 const waitMs = GuildDiscord::ComputeWaitMs(res.status, res.rateLimit);

            if (res.status >= 200 && res.status < 300)
            {
                if (waitMs)
                    SleepInterruptibly(waitMs);
                return;
            }

            bool const retryable = res.status == 0 || res.status == 429 || res.status >= 500;
            LOG_WARN("module.guilddiscord", "Webhook post for guild {} failed (status {}{}{}), attempt {}/{}.",
                msg.guildId, res.status, res.error.empty() ? "" : ", ", res.error, attempt, MAX_ATTEMPTS);

            if (!retryable || attempt == MAX_ATTEMPTS)
                return;

            if (!SleepInterruptibly(waitMs ? waitMs : 1000))
                return;
        }
    }

    void RunWorker()
    {
        while (auto msg = relayQueue.Pop())
        {
            if (relayStop)
                break;
            Deliver(*msg);
        }
    }

    void StartRelayWorker()
    {
        relayStop = false;
        relayWorker = std::thread(RunWorker);
    }

    void StopRelayWorker()
    {
        relayStop = true;
        relayQueue.Close();
        if (relayWorker.joinable())
            relayWorker.join();
    }
}

class GuildDiscordPlayerScript : public PlayerScript
{
public:
    GuildDiscordPlayerScript() : PlayerScript("GuildDiscordPlayerScript", {
        PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE
    }) { }

    // Officer chat is deliberately not relayed.
    void OnPlayerBeforeSendChatMessage(Player* player, uint32& type, uint32& lang, std::string& msg) override
    {
        if (type != CHAT_MSG_GUILD || lang == LANG_ADDON)
            return;

        if (!guildDiscordConfig.GetConfigValue<bool>(GuildDiscordConfig::ENABLED))
            return;

        uint32 const guildId = player->GetGuildId();
        if (!guildId)
            return;

        auto const it = guildTargets.find(guildId);
        if (it == guildTargets.end())
            return;

        std::string const content = GuildDiscord::SanitizeChat(msg);
        if (content.empty())
            return;

        relayQueue.Push({ it->second, guildId, GuildDiscord::BuildWebhookPayload(player->GetName(), content) });
    }
};

void AddGuildDiscordScripts()
{
    new GuildDiscordWorldScript();
    new GuildDiscordPlayerScript();
}
