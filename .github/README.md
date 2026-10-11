# mod-guild-discord

Discord integration for guilds on [AzerothCore](https://www.azerothcore.org) (WotLK 3.3.5a) servers.

> **Status:** Phase 0 only: one-way guild chat relay (game → Discord). Nothing else is implemented yet.

## Phase 0: guild chat relay

Guild chat from each mapped guild is posted to a Discord webhook under the character's name. Officer chat is never relayed. Mentions are disabled.

Map a guild to a webhook in the characters database (picked up on `.reload config`):

```sql
INSERT INTO mod_discord_guild_map (guild_id, webhook_url)
VALUES (1, 'https://discord.com/api/webhooks/<id>/<token>');
```

Then set `GuildDiscord.Enable = 1`. Treat webhook URLs as secrets.

Run the unit tests for the core-free logic with:

```
g++ -std=c++17 -Wall -Isrc tests/relay_tests.cpp -o /tmp/relay_tests -pthread && /tmp/relay_tests
```

## Planned features

- Two-way guild and officer chat bridge between the game and a Discord channel
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
