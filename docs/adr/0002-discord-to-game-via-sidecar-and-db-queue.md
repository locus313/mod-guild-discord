# Discord to game uses a sidecar bot and a database queue

The worldserver never holds a Discord Gateway connection. A separate Sidecar process (Node.js with discord.js, in `apps/discord-relay/`) owns the Gateway, reads only the mapped channels, and inserts rows into `mod_discord_relay_queue`. The module polls that table from a worker thread and broadcasts each row to the guild on the world thread. This keeps the bot token and the privileged Message Content intent out of the worldserver and avoids maintaining a Gateway client in C++.

## Considered options

- An in-process Gateway client was rejected because reconnect, resume and heartbeat logic would live inside the worldserver.
- Linking DPP was rejected because it is a large new build dependency.
- SOAP and the RA console were rejected because they require enabling remote access on the worldserver.

## Consequences

- The Sidecar gets a MySQL account limited to `SELECT` on `mod_discord_guild_map` and `INSERT` on `mod_discord_relay_queue`. The module deletes processed and stale rows.
- Rows older than 60 seconds are ignored, so a worldserver outage never replays old chat.
- Messages appear as a system-style `[Discord] Name: text` line rather than as a character, because no character session exists for the sender.
