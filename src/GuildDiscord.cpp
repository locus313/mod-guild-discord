/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "AchievementMgr.h"
#include "Announcements.h"
#include "ConfigValueCache.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "RelayQueue.h"
#include "RelayText.h"
#include "ScriptMgr.h"
#include "WebhookClient.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

enum class GuildDiscordConfig
{
    ENABLED,
    LEVEL_INTERVAL,

    NUM_CONFIGS,
};

class GuildDiscordConfigData : public ConfigValueCache<GuildDiscordConfig>
{
public:
    GuildDiscordConfigData() : ConfigValueCache(GuildDiscordConfig::NUM_CONFIGS) { };

    void BuildConfigCache() override
    {
        SetConfigValue<bool>(GuildDiscordConfig::ENABLED, "GuildDiscord.Enable", false);
        SetConfigValue<uint32>(GuildDiscordConfig::LEVEL_INTERVAL, "GuildDiscord.Announce.Level.Interval", 10);
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

    struct GuildEntry
    {
        GuildDiscord::WebhookTarget target;
        GuildDiscord::AnnounceToggles toggles;
    };

    using GuildMap = std::unordered_map<uint32, GuildEntry>;

    // Hooks can run on map-update threads while a reload runs on the world thread, so readers
    // take an immutable snapshot and reloads swap in a new one.
    std::mutex guildMapMutex;
    std::shared_ptr<GuildMap const> guildMap = std::make_shared<GuildMap const>();

    std::shared_ptr<GuildMap const> GetGuildMap()
    {
        std::lock_guard<std::mutex> lock(guildMapMutex);
        return guildMap;
    }

    GuildDiscord::ExpiringSet loginCooldown;
    GuildDiscord::ExpiringSet bossDedupe;
    std::atomic<uint64> startupMs{ 0 };

    uint64 NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void LoadGuildTargets()
    {
        auto loaded = std::make_shared<GuildMap>();

        QueryResult result = CharacterDatabase.Query(
            "SELECT `guild_id`, `webhook_url`, `announce_login`, `announce_logout`, `announce_level`, "
            "`announce_achievement`, `announce_bosskill` FROM `mod_discord_guild_map`");

        if (result)
        {
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

                GuildDiscord::AnnounceToggles toggles;
                toggles.login = fields[2].Get<bool>();
                toggles.logout = fields[3].Get<bool>();
                toggles.level = fields[4].Get<bool>();
                toggles.achievement = fields[5].Get<bool>();
                toggles.bossKill = fields[6].Get<bool>();

                loaded->emplace(guildId, GuildEntry{ std::move(*target), toggles });
            } while (result->NextRow());
        }

        LOG_INFO("module.guilddiscord", "Loaded {} guild webhook mapping(s).", loaded->size());

        std::lock_guard<std::mutex> lock(guildMapMutex);
        guildMap = std::move(loaded);
    }

    // Returns the entry only when the module is enabled and the guild is mapped.
    // The snapshot is returned too so the entry stays alive for the caller.
    std::pair<std::shared_ptr<GuildMap const>, GuildEntry const*> FindGuild(uint32 guildId)
    {
        if (!guildId || !guildDiscordConfig.GetConfigValue<bool>(GuildDiscordConfig::ENABLED))
            return { nullptr, nullptr };

        auto map = GetGuildMap();
        auto const it = map->find(guildId);
        if (it == map->end())
            return { nullptr, nullptr };

        GuildEntry const* entry = &it->second;
        return { std::move(map), entry };
    }

    void Enqueue(uint32 guildId, GuildEntry const& entry, std::string_view username, std::string_view content)
    {
        relayQueue.Push({ entry.target, guildId, GuildDiscord::BuildWebhookPayload(username, content) });
    }

    void Announce(uint32 guildId, GuildEntry const& entry, std::string const& content)
    {
        Enqueue(guildId, entry, GuildDiscord::ANNOUNCEMENT_USERNAME, content);
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
        startupMs = NowMs();
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
        PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE,
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_BEFORE_LOGOUT,
        PLAYERHOOK_ON_LEVEL_CHANGED,
        PLAYERHOOK_ON_ACHI_COMPLETE,
        PLAYERHOOK_ON_CREATURE_KILL
    }) { }

    // Officer chat is deliberately not relayed.
    void OnPlayerBeforeSendChatMessage(Player* player, uint32& type, uint32& lang, std::string& msg) override
    {
        if (type != CHAT_MSG_GUILD || lang == LANG_ADDON)
            return;

        auto const [snapshot, entry] = FindGuild(player->GetGuildId());
        if (!entry)
            return;

        std::string const content = GuildDiscord::SanitizeChat(msg);
        if (content.empty())
            return;

        Enqueue(player->GetGuildId(), *entry, player->GetName(), content);
    }

    // Logins and logouts are suppressed while a restart reconnects everyone.
    void OnPlayerLogin(Player* player) override
    {
        uint32 const guildId = player->GetGuildId();
        auto const [snapshot, entry] = FindGuild(guildId);
        if (!entry || !entry->toggles.login || GuildDiscord::InStartupGrace(NowMs(), startupMs))
            return;

        if (!loginCooldown.TryInsert(player->GetGUID().GetCounter(), NowMs(), GuildDiscord::LOGIN_COOLDOWN_MS))
            return;

        Announce(guildId, *entry, GuildDiscord::FormatLogin(player->GetName()));
    }

    // The guild id is still valid before logout teardown.
    void OnPlayerBeforeLogout(Player* player) override
    {
        uint32 const guildId = player->GetGuildId();
        auto const [snapshot, entry] = FindGuild(guildId);
        if (!entry || !entry->toggles.logout || GuildDiscord::InStartupGrace(NowMs(), startupMs))
            return;

        Announce(guildId, *entry, GuildDiscord::FormatLogout(player->GetName()));
    }

    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        uint32 const guildId = player->GetGuildId();
        auto const [snapshot, entry] = FindGuild(guildId);
        if (!entry || !entry->toggles.level)
            return;

        uint32 const level = player->GetLevel();
        uint32 const interval = guildDiscordConfig.GetConfigValue<uint32>(GuildDiscordConfig::LEVEL_INTERVAL);
        if (!GuildDiscord::ShouldAnnounceLevel(oldLevel, level, interval))
            return;

        Announce(guildId, *entry, GuildDiscord::FormatLevel(player->GetName(), level));
    }

    void OnPlayerAchievementComplete(Player* player, AchievementEntry const* achievement) override
    {
        if (!GuildDiscord::ShouldAnnounceAchievement(achievement->points))
            return;

        uint32 const guildId = player->GetGuildId();
        auto const [snapshot, entry] = FindGuild(guildId);
        if (!entry || !entry->toggles.achievement)
            return;

        char const* name = achievement->name[0]; // enUS
        Announce(guildId, *entry, GuildDiscord::FormatAchievement(player->GetName(), name ? name : "", achievement->points));
    }

    // Fires once per killer, so a raid would announce the same boss repeatedly without the dedupe.
    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        if (!killed->IsDungeonBoss())
            return;

        uint32 const guildId = killer->GetGuildId();
        auto const [snapshot, entry] = FindGuild(guildId);
        if (!entry || !entry->toggles.bossKill)
            return;

        uint64 const key = GuildDiscord::PairKey(guildId, static_cast<uint32>(killed->GetGUID().GetCounter()));
        if (!bossDedupe.TryInsert(key, NowMs(), GuildDiscord::BOSS_DEDUPE_MS))
            return;

        Announce(guildId, *entry, GuildDiscord::FormatBossKill(killer->GetName(), killed->GetName()));
    }
};

void AddGuildDiscordScripts()
{
    new GuildDiscordWorldScript();
    new GuildDiscordPlayerScript();
}
