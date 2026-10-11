/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef GUILD_DISCORD_WEBHOOK_CLIENT_H
#define GUILD_DISCORD_WEBHOOK_CLIENT_H

#include "RateLimit.h"
#include "RelayText.h"

#include <string>

namespace GuildDiscord
{
    struct PostResult
    {
        unsigned status = 0; // 0 means a transport error occurred
        RateLimitHeaders rateLimit;
        std::string error;
    };

    // Blocking HTTPS POST of a JSON payload. Must only be called from the worker thread.
    PostResult PostWebhook(WebhookTarget const& target, std::string const& jsonPayload);
}

#endif
