# mod-guild-discord

Discord integration for guilds on [AzerothCore](https://www.azerothcore.org) (WotLK 3.3.5a) servers.

> **Status:** Phase 2: guild chat relay and announcements (game → Discord), plus Discord → game through the [`discord-relay`](../apps/discord-relay) sidecar.

## Guild chat relay and announcements

Guild chat from each mapped guild is posted to a Discord webhook under the character's name. Officer chat is never relayed. Mentions are disabled.

Map a guild to a webhook in the characters database (picked up on `.reload config`):

```sql
INSERT INTO mod_discord_guild_map (guild_id, webhook_url)
VALUES (1, 'https://discord.com/api/webhooks/<id>/<token>');
```

Announcements post as "Guild Events" and are off until enabled per guild:

```sql
UPDATE mod_discord_guild_map SET announce_login = 1, announce_level = 1, announce_bosskill = 1 WHERE guild_id = 1;
```

Columns: `announce_login`, `announce_logout`, `announce_level`, `announce_achievement`, `announce_bosskill`. Level-ups post at multiples of `GuildDiscord.Announce.Level.Interval` and at level 80, achievements with 0 points are skipped, and login/logout notices pause for 60 seconds after startup.

Then set `GuildDiscord.Enable = 1`. Treat webhook URLs as secrets.

## Discord to game

A sidecar bot ([`apps/discord-relay`](../apps/discord-relay)) reads a Discord channel and inserts rows into `mod_discord_relay_queue`; the module polls that table about once a second and shows each message to online guild members as a green `[Discord] Name: text` line. Mentions and `|` escapes are stripped, long messages are split into up to 3 lines, and rows older than 60 seconds are dropped.

Bind a channel with `UPDATE mod_discord_guild_map SET discord_channel_id = <channel id> WHERE guild_id = 1;`. See the sidecar README for the bot and scoped database account setup.

Run the unit tests for the core-free logic with:

```
g++ -std=c++17 -Wall -Isrc tests/relay_tests.cpp -o /tmp/relay_tests -pthread && /tmp/relay_tests
```

## Planned features

- Officer chat bridge
- Guild event announcements: logins, level-ups, achievements, boss kills, member joins and leaves
- Account linking between Discord users and game accounts
- Guild rank to Discord role sync

No client patch is needed. Everything goes through standard server chat packets.

See [`docs/research.md`](../docs/research.md) for prior-art analysis, architecture and the phased plan.

## Installation

1. Clone into your AzerothCore `modules/` directory:
   ```
   git clone https://github.com/locus313/mod-guild-discord.git modules/mod-guild-discord
   ```
2. Re-run CMake and rebuild.
3. Copy `conf/mod-guild-discord.conf.dist` to `mod-guild-discord.conf` in your config directory and edit it.

## License

MIT, inherited from the skeleton-module template. See [LICENSE](../LICENSE).
