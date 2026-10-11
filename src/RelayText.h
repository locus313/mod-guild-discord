/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef GUILD_DISCORD_RELAY_TEXT_H
#define GUILD_DISCORD_RELAY_TEXT_H

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace GuildDiscord
{
    constexpr std::size_t DISCORD_CONTENT_MAX = 2000;

    inline bool IsHex(char c)
    {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    // Reduces WoW chat escapes to plain text: |cAARRGGBB, |r, |Hlink|h[Name]|h -> [Name], |Ttexture|t, || -> |
    inline std::string StripWowEscapes(std::string_view in)
    {
        std::string out;
        out.reserve(in.size());

        for (std::size_t i = 0; i < in.size(); ++i)
        {
            if (in[i] != '|' || i + 1 >= in.size())
            {
                out += in[i];
                continue;
            }

            char const next = in[i + 1];
            if (next == '|')
            {
                out += '|';
                ++i;
            }
            else if (next == 'c' || next == 'C')
            {
                std::size_t end = i + 2;
                while (end < in.size() && end < i + 10 && IsHex(in[end]))
                    ++end;
                i = (end == i + 10) ? end - 1 : i + 1;
            }
            else if (next == 'r' || next == 'R' || next == 'h')
                ++i;
            else if (next == 'H')
            {
                std::size_t const close = in.find("|h", i + 2);
                if (close == std::string_view::npos)
                    ++i;
                else
                    i = close + 1;
            }
            else if (next == 'T')
            {
                std::size_t const close = in.find("|t", i + 2);
                if (close == std::string_view::npos)
                    ++i;
                else
                    i = close + 1;
            }
            else
                out += in[i];
        }

        return out;
    }

    inline std::string EscapeDiscordMarkdown(std::string_view in)
    {
        std::string out;
        out.reserve(in.size());
        for (char c : in)
        {
            switch (c)
            {
                case '\\': case '*': case '_': case '~': case '`': case '>': case '|': case '#':
                    out += '\\';
                    break;
                default:
                    break;
            }
            out += c;
        }
        return out;
    }

    // Truncates to maxBytes without splitting a UTF-8 sequence.
    inline std::string TruncateUtf8(std::string s, std::size_t maxBytes)
    {
        if (s.size() <= maxBytes)
            return s;

        std::size_t cut = maxBytes;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
            --cut;
        s.resize(cut);
        return s;
    }

    inline std::string SanitizeChat(std::string_view in)
    {
        return TruncateUtf8(EscapeDiscordMarkdown(StripWowEscapes(in)), DISCORD_CONTENT_MAX);
    }

    inline std::string JsonEscape(std::string_view in)
    {
        std::string out;
        out.reserve(in.size() + 2);
        for (char c : in)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                        out += buf;
                    }
                    else
                        out += c;
            }
        }
        return out;
    }

    // Webhook usernames are 1-80 chars and may not be empty.
    inline std::string BuildWebhookPayload(std::string_view username, std::string_view content)
    {
        std::string name = TruncateUtf8(std::string(username), 80);
        if (name.empty())
            name = "Unknown";

        return "{\"username\":\"" + JsonEscape(name) + "\",\"content\":\"" + JsonEscape(content) +
               "\",\"allowed_mentions\":{\"parse\":[]}}";
    }

    struct WebhookTarget
    {
        std::string host;
        std::string target;
    };

    // Accepts only https Discord webhook URLs of the form <prefix><id>/<token>.
    inline std::optional<WebhookTarget> ParseWebhookUrl(std::string_view url)
    {
        for (std::string_view host : { std::string_view("discord.com"), std::string_view("discordapp.com") })
        {
            std::string const prefix = "https://" + std::string(host) + "/api/webhooks/";
            if (url.substr(0, prefix.size()) != prefix)
                continue;

            std::string_view const rest = url.substr(prefix.size());
            std::size_t const slash = rest.find('/');
            if (slash == std::string_view::npos || slash == 0 || slash + 1 >= rest.size())
                return std::nullopt;

            for (std::size_t i = 0; i < rest.size(); ++i)
            {
                char const c = rest[i];
                bool const ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                c == '-' || c == '_' || (c == '/' && i == slash);
                if (!ok)
                    return std::nullopt;
            }

            return WebhookTarget{ std::string(host), "/api/webhooks/" + std::string(rest) };
        }

        return std::nullopt;
    }
}

#endif
