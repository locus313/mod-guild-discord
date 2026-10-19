/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef GUILD_DISCORD_ANNOUNCEMENTS_H
#define GUILD_DISCORD_ANNOUNCEMENTS_H

#include "RelayText.h"

#include <cstdint>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace GuildDiscord
{
    constexpr char const* ANNOUNCEMENT_USERNAME = "Guild Events";
    constexpr std::uint32_t ALWAYS_ANNOUNCE_LEVEL = 80;
    constexpr std::uint64_t STARTUP_GRACE_MS = 60 * 1000;
    constexpr std::uint64_t LOGIN_COOLDOWN_MS = 5 * 60 * 1000;
    constexpr std::uint64_t BOSS_DEDUPE_MS = 10 * 60 * 1000;

    struct AnnounceToggles
    {
        bool login = false;
        bool logout = false;
        bool level = false;
        bool achievement = false;
        bool bossKill = false;
    };

    // Hooks may run on map-update threads, so this locks internally.
    class ExpiringSet
    {
    public:
        // True if the key was absent or expired (and is now recorded); false if still within ttl.
        bool TryInsert(std::uint64_t key, std::uint64_t nowMs, std::uint64_t ttlMs)
        {
            std::lock_guard<std::mutex> lock(_mutex);

            auto const it = _entries.find(key);
            if (it != _entries.end() && nowMs < it->second)
                return false;

            if (_entries.size() >= PRUNE_THRESHOLD)
                Prune(nowMs);

            _entries[key] = nowMs + ttlMs;
            return true;
        }

    private:
        static constexpr std::size_t PRUNE_THRESHOLD = 4096;

        void Prune(std::uint64_t nowMs)
        {
            for (auto it = _entries.begin(); it != _entries.end();)
                it = (nowMs >= it->second) ? _entries.erase(it) : std::next(it);
        }

        std::mutex _mutex;
        std::unordered_map<std::uint64_t, std::uint64_t> _entries;
    };

    inline std::uint64_t PairKey(std::uint32_t high, std::uint32_t low)
    {
        return (static_cast<std::uint64_t>(high) << 32) | low;
    }

    // Interval 0 leaves only the max level.
    inline bool ShouldAnnounceLevel(std::uint32_t oldLevel, std::uint32_t newLevel, std::uint32_t interval)
    {
        if (newLevel <= oldLevel)
            return false;
        if (newLevel == ALWAYS_ANNOUNCE_LEVEL)
            return true;
        return interval != 0 && newLevel % interval == 0;
    }

    inline bool ShouldAnnounceAchievement(std::uint32_t points)
    {
        return points > 0;
    }

    inline bool InStartupGrace(std::uint64_t nowMs, std::uint64_t startMs)
    {
        return nowMs < startMs + STARTUP_GRACE_MS;
    }

    inline std::string FormatLogin(std::string_view name)
    {
        return "**" + EscapeDiscordMarkdown(name) + "** has come online";
    }

    inline std::string FormatLogout(std::string_view name)
    {
        return "**" + EscapeDiscordMarkdown(name) + "** has gone offline";
    }

    inline std::string FormatLevel(std::string_view name, std::uint32_t level)
    {
        return "**" + EscapeDiscordMarkdown(name) + "** has reached level " + std::to_string(level);
    }

    inline std::string FormatAchievement(std::string_view name, std::string_view achievement, std::uint32_t points)
    {
        return "**" + EscapeDiscordMarkdown(name) + "** has earned the achievement **" +
               EscapeDiscordMarkdown(achievement) + "** (" + std::to_string(points) + " points)";
    }

    inline std::string FormatBossKill(std::string_view name, std::string_view boss)
    {
        return "**" + EscapeDiscordMarkdown(name) + "** has slain **" + EscapeDiscordMarkdown(boss) + "**";
    }
}

#endif
