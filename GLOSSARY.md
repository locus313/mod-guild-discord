# Guild Discord

An AzerothCore module that bridges a WoW guild's activity to a Discord channel.

## Language

**WoW Guild**:
An in-game guild on the realm, identified by its guild id.
_Avoid_: Discord guild, server, clan

**Guild Chat**:
Messages sent on the guild channel (`CHAT_MSG_GUILD`). Officer chat is a separate channel and is never relayed.
_Avoid_: Guild messages, guild text

**Relay**:
Forwarding a Guild Chat message from the game to a Discord channel. Phase 0 relays one way only: game to Discord.
_Avoid_: Bridge, sync, mirror

**Guild Webhook Mapping**:
The admin-configured link from one WoW Guild to one Discord webhook. A WoW Guild without a mapping is never relayed.
_Avoid_: Guild map, channel link

**Announcement**:
An automated notice about a guild member event (login, logout, level-up, achievement, boss kill), posted under the fixed name "Guild Events". Distinct from relayed Guild Chat.
_Avoid_: Event message, notification, alert

**Announcement Toggle**:
A per-guild on/off switch for one Announcement type, stored on that guild's Guild Webhook Mapping. All are off by default.
_Avoid_: Event setting, feature flag

**Relay Queue**:
The bounded buffer of pending outbound messages. When full, the oldest message is dropped.
_Avoid_: Message buffer, outbox
