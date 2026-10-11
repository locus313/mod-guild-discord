/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef GUILD_DISCORD_RATE_LIMIT_H
#define GUILD_DISCORD_RATE_LIMIT_H

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>

namespace GuildDiscord
{
    constexpr std::uint32_t MAX_WAIT_MS = 60000;
    constexpr std::uint32_t DEFAULT_429_WAIT_MS = 1000;

    inline std::optional<double> ParseSeconds(std::string const& value)
    {
        if (value.empty())
            return std::nullopt;

        char* end = nullptr;
        double const v = std::strtod(value.c_str(), &end);
        if (end == value.c_str() || v < 0)
            return std::nullopt;
        return v;
    }

    struct RateLimitHeaders
    {
        std::string retryAfter;
        std::string remaining;
        std::string resetAfter;
    };

    // How long the worker should pause before the next request, in milliseconds.
    inline std::uint32_t ComputeWaitMs(unsigned status, RateLimitHeaders const& h)
    {
        std::optional<double> seconds;

        if (status == 429)
            seconds = ParseSeconds(h.retryAfter).value_or(DEFAULT_429_WAIT_MS / 1000.0);
        else if (ParseSeconds(h.remaining) == 0.0)
            seconds = ParseSeconds(h.resetAfter);

        if (!seconds)
            return 0;

        double const ms = std::min(*seconds * 1000.0, static_cast<double>(MAX_WAIT_MS));
        return static_cast<std::uint32_t>(ms);
    }
}

#endif
