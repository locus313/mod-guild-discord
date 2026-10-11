# mod-guild-discord

Discord integration for guilds on [AzerothCore](https://www.azerothcore.org) (WotLK 3.3.5a) servers.

> **Status:** early scaffold. The module compiles and loads its config but has no features yet.

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
