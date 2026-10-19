# discord-relay

Sidecar that owns the Discord Gateway connection and queues channel messages for the `mod-guild-discord` module.

## Setup

1. In the Discord developer portal, create a bot, enable the **Message Content** intent, and invite it to your server with permission to read the mapped channels.
2. Map a channel to a guild (the module reads this table, the sidecar reloads it every minute):

   ```sql
   UPDATE mod_discord_guild_map SET discord_channel_id = <channel id> WHERE guild_id = <guild id>;
   ```

3. Create a scoped database account:

   ```sql
   CREATE USER 'discord_relay'@'%' IDENTIFIED BY '<password>';
   GRANT SELECT ON acore_characters.mod_discord_guild_map TO 'discord_relay'@'%';
   GRANT INSERT ON acore_characters.mod_discord_relay_queue TO 'discord_relay'@'%';
   ```

4. Copy `.env.example` to `.env` and fill it in.
5. Run it: `npm install && npm start`, `docker build -t discord-relay .`, or install `discord-relay.service`.

Bot and webhook messages are ignored, and each guild is limited to 5 messages per 10 seconds.

## Tests

`npm test`
