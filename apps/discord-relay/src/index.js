'use strict';

const { Client, GatewayIntentBits, Events } = require('discord.js');
const mysql = require('mysql2/promise');
const { toQueueRow, RateLimiter } = require('./relay');

const env = (k, d) => process.env[k] ?? d;
const token = env('DISCORD_TOKEN');
if (!token) {
  console.error('DISCORD_TOKEN is required');
  process.exit(1);
}

const pool = mysql.createPool({
  host: env('DB_HOST', '127.0.0.1'),
  port: Number(env('DB_PORT', '3306')),
  user: env('DB_USER'),
  password: env('DB_PASSWORD'),
  database: env('DB_NAME', 'acore_characters'),
  connectionLimit: 3,
});

let channelToGuild = new Map();
const limiter = new RateLimiter(5, 10_000);

async function reloadMap() {
  try {
    const [rows] = await pool.query(
      'SELECT guild_id, discord_channel_id FROM mod_discord_guild_map WHERE discord_channel_id IS NOT NULL',
    );
    channelToGuild = new Map(rows.map((r) => [String(r.discord_channel_id), r.guild_id]));
  } catch (err) {
    console.error('guild map reload failed:', err.message);
  }
}

const client = new Client({
  intents: [GatewayIntentBits.Guilds, GatewayIntentBits.GuildMessages, GatewayIntentBits.MessageContent],
});

client.on(Events.MessageCreate, async (message) => {
  const row = toQueueRow(message, channelToGuild);
  if (!row || !limiter.allow(row.wowGuildId)) return;
  try {
    await pool.execute(
      'INSERT INTO mod_discord_relay_queue (wow_guild_id, discord_user_id, display_name, message) VALUES (?, ?, ?, ?)',
      [row.wowGuildId, row.discordUserId, row.displayName, row.message],
    );
  } catch (err) {
    console.error('queue insert failed:', err.message);
  }
});

client.once(Events.ClientReady, (c) => console.log(`logged in as ${c.user.tag}`));

(async () => {
  await reloadMap();
  setInterval(reloadMap, Number(env('MAP_RELOAD_SECONDS', '60')) * 1000);
  await client.login(token);
})();
