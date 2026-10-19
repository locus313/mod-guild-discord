/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef GUILD_DISCORD_INBOUND_TEXT_H
#define GUILD_DISCORD_INBOUND_TEXT_H

#include "RelayText.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace GuildDiscord
{
    constexpr std::size_t INBOUND_CHUNK_BYTES = 240;
    constexpr std::size_t INBOUND_MAX_CHUNKS = 3;
    constexpr std::size_t INBOUND_NAME_BYTES = 32;
    constexpr char const* INBOUND_PREFIX = "[Discord] ";

    // Turns <@id>, <@&id>, <#id> into nothing and <:name:id> / <a:name:id> into :name:.
    // Drops '|' (WoW escape introducer) and control characters, and collapses whitespace.
    inline std::string SanitizeDiscordText(std::string_view in)
    {
        std::string out;
        out.reserve(in.size());
        bool lastSpace = true;

        auto push = [&](char c)
        {
            bool const space = c == ' ' || c == '\n' || c == '\r' || c == '\t';
            if (space)
            {
                if (!lastSpace)
                    out += ' ';
                lastSpace = true;
                return;
            }
            if (c == '|' || (static_cast<unsigned char>(c) < 0x20) || c == 0x7f)
                return;
            out += c;
            lastSpace = false;
        };

        for (std::size_t i = 0; i < in.size(); ++i)
        {
            if (in[i] == '<')
            {
                std::size_t const close = in.find('>', i);
                if (close != std::string_view::npos && close - i <= 64)
                {
                    std::string_view const tag = in.substr(i + 1, close - i - 1);
                    if (tag.empty())
                    {
                        push(in[i]);
                        continue;
                    }

                    if (tag[0] == '@' || tag[0] == '#')
                    {
                        i = close;
                        continue;
                    }

                    std::size_t const nameStart = tag.substr(0, 2) == "a:" ? 2 : (tag[0] == ':' ? 1 : std::string_view::npos);
                    if (nameStart != std::string_view::npos)
                    {
                        std::size_t const nameEnd = tag.find(':', nameStart);
                        if (nameEnd != std::string_view::npos && nameEnd > nameStart)
                        {
                            push(':');
                            for (char c : tag.substr(nameStart, nameEnd - nameStart))
                                push(c);
                            push(':');
                            i = close;
                            continue;
                        }
                    }
                }
            }
            push(in[i]);
        }

        while (!out.empty() && out.back() == ' ')
            out.pop_back();
        return out;
    }

    // Splits on spaces; words longer than maxBytes are cut at a UTF-8 boundary.
    inline std::vector<std::string> SplitWords(std::string const& text, std::size_t maxBytes)
    {
        std::vector<std::string> chunks;
        std::string current;

        auto flush = [&]
        {
            if (!current.empty())
                chunks.push_back(std::move(current));
            current.clear();
        };

        std::size_t pos = 0;
        while (pos < text.size())
        {
            std::size_t end = text.find(' ', pos);
            if (end == std::string::npos)
                end = text.size();

            std::string word = text.substr(pos, end - pos);
            pos = end + 1;

            while (word.size() > maxBytes)
            {
                flush();
                std::string head = TruncateUtf8(word, maxBytes);
                if (head.empty())
                    return chunks;
                word.erase(0, head.size());
                chunks.push_back(std::move(head));
            }

            if (current.empty())
                current = std::move(word);
            else if (current.size() + 1 + word.size() <= maxBytes)
                current += ' ' + word;
            else
            {
                flush();
                current = std::move(word);
            }
        }

        flush();
        return chunks;
    }

    // Returns the ready-to-send lines for one Discord message (empty if there is nothing to say).
    inline std::vector<std::string> FormatInbound(std::string_view displayName, std::string_view message)
    {
        std::string name = TruncateUtf8(SanitizeDiscordText(displayName), INBOUND_NAME_BYTES);
        if (name.empty())
            name = "Unknown";

        std::string const prefix = std::string(INBOUND_PREFIX) + name + ": ";
        std::string const text = SanitizeDiscordText(message);

        std::vector<std::string> chunks = SplitWords(text, INBOUND_CHUNK_BYTES - prefix.size());
        if (chunks.empty())
            return {};

        if (chunks.size() > INBOUND_MAX_CHUNKS)
        {
            chunks.resize(INBOUND_MAX_CHUNKS);
            chunks.back() = TruncateUtf8(chunks.back(), INBOUND_CHUNK_BYTES - prefix.size() - 3) + "...";
        }

        for (std::string& chunk : chunks)
            chunk = prefix + chunk;
        return chunks;
    }
}

#endif
