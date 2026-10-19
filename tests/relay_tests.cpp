// Standalone tests for the core-free relay logic.
// Build and run: g++ -std=c++17 -Wall -Isrc tests/relay_tests.cpp -o /tmp/relay_tests -pthread && /tmp/relay_tests

#include "Announcements.h"
#include "InboundText.h"
#include "RateLimit.h"
#include "RelayQueue.h"
#include "RelayText.h"

#include <cstdlib>
#include <iostream>

using namespace GuildDiscord;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; ++failures; } } while (0)

int main()
{
    CHECK(StripWowEscapes("|cff1eff00|Hitem:19019::::::::80:::::|h[Thunderfury]|h|r rocks") == "[Thunderfury] rocks");
    CHECK(StripWowEscapes("a||b") == "a|b");
    CHECK(StripWowEscapes("|TInterface\\Icon:0|t hi") == " hi");
    CHECK(StripWowEscapes("trailing |") == "trailing |");

    CHECK(SanitizeChat("*bold* @everyone _x_") == "\\*bold\\* @everyone \\_x\\_");
    CHECK(SanitizeChat(std::string(5000, 'a')).size() == DISCORD_CONTENT_MAX);

    // 2-byte char straddling the limit must not be split
    std::string utf8 = std::string(1999, 'a') + "\xC3\xA9";
    CHECK(TruncateUtf8(utf8, 2000) == std::string(1999, 'a'));

    CHECK(BuildWebhookPayload("Bob", "hi \"x\"\n") ==
          "{\"username\":\"Bob\",\"content\":\"hi \\\"x\\\"\\n\",\"allowed_mentions\":{\"parse\":[]}}");
    CHECK(BuildWebhookPayload("", "x").find("\"username\":\"Unknown\"") != std::string::npos);

    auto ok = ParseWebhookUrl("https://discord.com/api/webhooks/123/abc-DEF_9");
    CHECK(ok && ok->host == "discord.com" && ok->target == "/api/webhooks/123/abc-DEF_9");
    CHECK(ParseWebhookUrl("https://discordapp.com/api/webhooks/1/t"));
    CHECK(!ParseWebhookUrl("http://discord.com/api/webhooks/1/t"));
    CHECK(!ParseWebhookUrl("https://evil.com/api/webhooks/1/t"));
    CHECK(!ParseWebhookUrl("https://discord.com.evil.com/api/webhooks/1/t"));
    CHECK(!ParseWebhookUrl("https://discord.com/api/webhooks/1"));
    CHECK(!ParseWebhookUrl("https://discord.com/api/webhooks/1/t?wait=true"));
    CHECK(!ParseWebhookUrl("https://discord.com/api/webhooks/1/t/../x"));
    CHECK(!ParseWebhookUrl(""));

    RelayQueue<int> q(2);
    CHECK(!q.Push(1));
    CHECK(!q.Push(2));
    CHECK(q.Push(3));
    CHECK(q.Pop() == 2);
    CHECK(q.Pop() == 3);
    q.Close();
    CHECK(!q.Pop());
    CHECK(!q.Push(4));

    CHECK(ComputeWaitMs(429, { "2.5", "", "" }) == 2500);
    CHECK(ComputeWaitMs(429, { "", "", "" }) == DEFAULT_429_WAIT_MS);
    CHECK(ComputeWaitMs(429, { "9999", "", "" }) == MAX_WAIT_MS);
    CHECK(ComputeWaitMs(204, { "", "0", "1.5" }) == 1500);
    CHECK(ComputeWaitMs(204, { "", "3", "1.5" }) == 0);
    CHECK(ComputeWaitMs(204, { "", "", "" }) == 0);

    CHECK(ShouldAnnounceLevel(9, 10, 10));
    CHECK(!ShouldAnnounceLevel(10, 11, 10));
    CHECK(ShouldAnnounceLevel(79, 80, 10));
    CHECK(ShouldAnnounceLevel(79, 80, 7));
    CHECK(!ShouldAnnounceLevel(20, 10, 10));
    CHECK(!ShouldAnnounceLevel(9, 10, 0));
    CHECK(ShouldAnnounceLevel(79, 80, 0));

    CHECK(!ShouldAnnounceAchievement(0));
    CHECK(ShouldAnnounceAchievement(10));

    CHECK(InStartupGrace(59999, 0));
    CHECK(!InStartupGrace(60000, 0));

    ExpiringSet set;
    CHECK(set.TryInsert(1, 1000, 500));
    CHECK(!set.TryInsert(1, 1499, 500));
    CHECK(set.TryInsert(1, 1500, 500));
    CHECK(set.TryInsert(2, 1000, 500));
    CHECK(PairKey(1, 2) != PairKey(2, 1));

    CHECK(FormatLogin("Bob") == "**Bob** has come online");
    CHECK(FormatLevel("Bob", 20) == "**Bob** has reached level 20");
    CHECK(FormatAchievement("Bob", "Level *10*", 10) ==
          "**Bob** has earned the achievement **Level \\*10\\*** (10 points)");
    CHECK(FormatBossKill("Bob", "Ragnaros_") == "**Bob** has slain **Ragnaros\\_**");

    CHECK(SanitizeDiscordText("hi <@123> and <@!4> <@&5> <#6>!") == "hi and !");
    CHECK(SanitizeDiscordText("<:kek:123> <a:dance:9>") == ":kek: :dance:");
    CHECK(SanitizeDiscordText("a\n\n  b\t|cffff0000red|r") == "a b cffff0000redr");
    CHECK(SanitizeDiscordText("<>") == "<>");
    CHECK(SanitizeDiscordText("   ") == "");

    auto lines = FormatInbound("Bob", "hello world");
    CHECK(lines.size() == 1 && lines[0] == "[Discord] Bob: hello world");
    CHECK(FormatInbound("Bob", "<@1>").empty());
    CHECK(FormatInbound("", "x")[0] == "[Discord] Unknown: x");

    {
        std::string longText;
        for (int i = 0; i < 200; ++i)
            longText += "word ";
        auto chunks = FormatInbound("Bob", longText);
        CHECK(chunks.size() == INBOUND_MAX_CHUNKS);
        CHECK(chunks.back().size() >= 3 && chunks.back().substr(chunks.back().size() - 3) == "...");
        for (auto const& c : chunks)
            CHECK(c.size() <= INBOUND_CHUNK_BYTES);

        auto big = FormatInbound("Bob", std::string(1000, 'x'));
        CHECK(!big.empty());
        for (auto const& c : big)
            CHECK(c.size() <= INBOUND_CHUNK_BYTES);

        // multibyte text must never be cut mid-character
        std::string accents;
        for (int i = 0; i < 300; ++i)
            accents += "\xC3\xA9";
        for (auto const& c : FormatInbound("B\xC3\xA9", accents))
            CHECK(c.size() <= INBOUND_CHUNK_BYTES && (static_cast<unsigned char>(c.back()) < 0x80 || static_cast<unsigned char>(c[c.size() - 2]) == 0xC3));
    }

    if (failures == 0)
        std::cout << "all tests passed\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
