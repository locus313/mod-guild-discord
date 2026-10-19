'use strict';

const MAX_MESSAGE_LENGTH = 2000;

// Returns the queue row for a Discord message, or null when it must not be relayed.
// Bot and webhook messages are skipped so our own outbound relay never echoes back.
function toQueueRow(message, channelToGuild) {
  if (!message || message.author?.bot || message.webhookId) return null;

  const guildId = channelToGuild.get(String(message.channelId));
  if (guildId === undefined) return null;

  const text = String(message.cleanContent ?? '').trim();
  if (!text) return null;

  const name = message.member?.displayName ?? message.author?.username ?? 'Unknown';
  return {
    wowGuildId: guildId,
    discordUserId: String(message.author.id),
    displayName: name.slice(0, 64),
    message: text.slice(0, MAX_MESSAGE_LENGTH),
  };
}

// Sliding-window limiter keyed per WoW guild.
class RateLimiter {
  constructor(limit, windowMs, now = Date.now) {
    this.limit = limit;
    this.windowMs = windowMs;
    this.now = now;
    this.hits = new Map();
  }

  allow(key) {
    const t = this.now();
    const recent = (this.hits.get(key) ?? []).filter((h) => t - h < this.windowMs);
    if (recent.length >= this.limit) {
      this.hits.set(key, recent);
      return false;
    }
    recent.push(t);
    this.hits.set(key, recent);
    return true;
  }
}

module.exports = { toQueueRow, RateLimiter, MAX_MESSAGE_LENGTH };
