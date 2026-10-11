# AzerothCore (3.3.5a / build 12340) Guild ↔ Discord Integration Module — Research Report

Date: 2026-10-11
Scope: Investigate prior art for a guild-focused Discord bridge module for AzerothCore, then propose
a verified architecture grounded in the actual AzerothCore source and Discord's own developer docs.

---

## 1. Summary / verdict

**Prior art exists, but nothing that fully matches the described idea (guild chat↔Discord bridge +
online/offline notices + achievement/level/boss-kill announcements + account linking + rank/role
sync) in one module.** What exists today splits into three different problem spaces, none of which is
a complete match:

1. **Realm-wide (not guild-specific) chat/event bridges** — e.g. `kiwilabagency/mod-discord-chat`,
   which is the closest technical analogue: a native, in-process C++ Discord gateway client with
   two-way chat, login/logout/achievement/boss-kill event logging, presence, and webhooks. But it
   bridges the server's **Global/World chat channel**, not **guild chat**, and ships with **no account
   linking and no guild-rank synchronization** (verified by reading its source — no `GuildScript`
   hook is used anywhere in the codebase). License: unspecified/"Other" (no SPDX-recognized license
   file content was retrievable; treat as **all-rights-reserved unless clarified** — unverified).
2. **GM-ticket-to-Discord bridges**, e.g. `Calmoran/mod-heimdall` — two-way GM support ticket
   bridge, AGPL-3.0, actively maintained (pushed 2026-09), but explicitly **not** a guild chat bridge
   and explicitly states it does *not* do Discord↔game account linking.
3. **Thin webhook utility libraries**, e.g. `Exitare/mod-discord-webhook` — a generic
   "send any string to a Discord webhook" helper (`WebhookMgr::ScheduleMessage`) hooked from
   `WorldScript`/`PlayerScript`. MIT license. It provides the transport primitive only; a consumer
   module would still have to add all guild logic, achievement/boss-kill hooks, and account linking
   themselves.

No repository found (searched `org:azerothcore`, GitHub topic `azerothcore-module` + `discord`,
and free-text search `azerothcore discord`) combines **guild-scoped** chat relay, **guild roster**
online/offline notices, achievement/level/boss-kill guild announcements, **Discord↔account linking**,
and **guild rank → Discord role sync** in a single, actively maintained module. This is a genuine
gap and a reasonable new module to build. Eluna (Lua) prior art is similarly thin: no public
Eluna script implementing a full guild↔Discord bridge was found; Eluna's own hook set is the
relevant constraint (see §5/Eluna below), not a ready-made solution.

**Scope note on GUILDHOOK coverage:** AzerothCore's `GuildScript` has hooks for member add/remove,
MOTD, info, bank events, and create/disband, but **no dedicated "guild chat message" hook** — guild
chat is not routed through `GuildScript` at all. It is handled generically through
`PlayerScript::OnPlayerBeforeSendChatMessage` (fires for every outgoing chat type, filtered by the
caller to `CHAT_MSG_GUILD`/`CHAT_MSG_OFFICER`) and through `Guild::BroadcastToGuild()` in
`Guild.cpp`. This is a verified, important architectural detail (§4).

---

## 2. Prior-art table

| Repo | Last commit (verified) | Stars | Features verified from source | License | Gaps vs. the full idea |
|---|---|---|---|---|---|
| [`kiwilabagency/mod-discord-chat`](https://github.com/kiwilabagency/mod-discord-chat) | 2026-09-09 (`9ffe92d`) | 2 | In-process C++ Discord **gateway** client (`DiscordGateway.cpp`) + REST (`DiscordHttp.cpp`) on a dedicated worker thread (README: "All Discord networking runs on a dedicated worker thread"); two-way **global** chat bridge (`GlobalChatProvider.cpp`) depending on an external `mod-global-chat`/`mod-world-chat`; webhook-based message relay with class/faction icon prefixes; event logging via `DiscordEvents` class with verified methods `OnPlayerLogin`, `OnPlayerLogout`, `OnAchievementComplete`, `OnCreatureKill` (boss-kill detection via `IsBossKillEnabled`), wired from `DiscordPlayerScript::OnPlayerAchievementComplete`/`OnPlayerCreatureKill` in `DiscordModuleScripts.cpp:85-94`; in-game `.discord` commands via `CommandScript`; Discord bot presence/status. | "Other" / unrecognized license text starting "GNU AFFERO GENERAL PUBLIC LICENSE Version 3" at the top of `LICENSE`, but GitHub's API classifies it `NOASSERTION` — **treat licensing as unverified/ambiguous**, confirm by reading the full `LICENSE` file before reuse. | No `GuildScript` usage anywhere in the codebase (grepped `DiscordModuleScripts.cpp` — zero matches for "Guild"): it is a realm-wide bridge, not guild-scoped. No Discord-account-to-game-account linking. No guild-rank→Discord-role sync (it does have a generic `.discord setup roles` that creates faction/class roles, not guild-rank roles). Requires a *third-party* global-chat module as a dependency. |
| [`Calmoran/mod-heimdall`](https://github.com/Calmoran/mod-heimdall) | 2026-09-05 (per repo `pushed_at`) | 4 | GM-ticket↔Discord two-way bridge; server module (C++) + separate Node.js bot; ticket queue channel, claim tracking, per-account ticket history, GM note system; replies delivered as in-game whispers split into 240-byte segments (README). | AGPL-3.0-or-later (explicit, covers both module and bot) | Not a guild chat bridge at all — different feature (support tickets). README explicitly states: "It does not link Discord accounts to game accounts" and "does not do... rank sync." Needs a core patch (`0001-expose-loginqueryholder-to-modules.patch`) applied to AzerothCore itself. |
| [`Exitare/mod-discord-webhook`](https://github.com/Exitare/mod-discord-webhook) | repo `pushed_at` 2026-01-17 | 6 | Minimal webhook transport: `WebhookMgr::ScheduleMessage(message)` queues a string; `WebhookServerScripts : WorldScript` (`OnStartup`/`OnShutdown`) starts/stops the queue; example in README wires `WebhookPlayerLogin : PlayerScript::OnLogin` to post a login message. Confirmed via `src/server_hooks.cpp` and `src/webhook_loader.cpp`. | MIT (GitHub API `license.key: mit`) | Only a thin plain-text-webhook transport primitive — no guild awareness, no two-way (Discord→game) path, no achievement/level/boss-kill hooks built in (left to the integrator), no account linking, no rank sync. |
| [`Diabloxx/mod-gm-discord`](https://github.com/Diabloxx/mod-gm-discord) | not verified beyond search listing ("GM to Discord Tools") | not captured | Not deep-dived (description indicates GM-tooling bridge, same family as mod-heimdall) | not verified | Not guild-scoped (unverified assumption based on description only — **inferred**, not read from source). |
| [`quadseven/mod-overseer`](https://github.com/quadseven/mod-overseer) | not verified beyond search listing | not captured | Description: "control plane for mod-playerbots - Discord/DB command bridge, drives quests/travel/professions" — a playerbot command/control bridge, not a guild chat bridge. | not verified | Different problem domain entirely (playerbot automation control). Not examined further — **unverified** beyond README description. |
| [`Griznah/azerothcore-discord`](https://github.com/Griznah/azerothcore-discord) | pushed 2026-10-11 | 0 | Tagged `azerothcore-tools`, written in Go — i.e., an **external sidecar bot**, not an in-core module. Description: "Discord (and more) bot for AzerothCore." Not deep-dived beyond repo metadata. | GPL-3.0 | Unverified feature set — too new/undocumented to assess against the guild-bridge checklist (**unverified**). |

No module matching the search terms was found directly under the `github.com/azerothcore` org
itself (the org's own repos are the core + a curated set of "official" `mod-*` modules; a live
catalogue search of https://www.azerothcore.org/catalogue.html could not be completed because the
page is a client-side-rendered app with no server-rendered listing — **unverified/inaccessible** via
`web_fetch`; topics `azerothcore-module`, `azerothcore-tools`, `azerothcore-lua`, `azerothcore-sql`
back it, and the GitHub topic search above is the closest verifiable proxy for that catalogue).
No Eluna (`ElunaLuaEngine/Eluna`, `azerothcore/mod-eluna-engine`) script implementing a guild↔Discord
bridge was found in the time available — **unverified absence**, i.e., a negative result from a
search, not a certified non-existence claim. TrinityCore/CMaNGOS/MaNGOS ecosystem Discord bridges
were not individually deep-dived beyond noting that the same generic webhook/gateway patterns
documented here (webhook POST, external bot + DB polling) are the standard approaches across all of
these emulators since they share the same WotLK packet structure — **inferred**, not verified against
a specific TrinityCore/MaNGOS repo.

---

## 3. Recommended architecture

### 3.1 Component diagram

```
                     ┌─────────────────────────────────────────────┐
                     │              AzerothCore worldserver          │
                     │                                                │
  Guild chat/events  │  ┌───────────────┐    ┌────────────────────┐  │
  (game thread) ────►│  │ PlayerScript/  │    │  mod-guild-discord  │  │
                     │  │ GuildScript/   │───►│  (ScriptObject impls│  │
                     │  │ AllCreatureScr.│    │  + in-memory queue) │  │
                     │  └───────────────┘    └──────────┬─────────┘  │
                     │                                   │ push      │
                     │                          ┌────────▼─────────┐ │
                     │                          │  Worker thread    │ │
                     │                          │  (std::thread +   │ │
                     │                          │  queue, NEVER on  │ │
                     │                          │  the World::Update│ │
                     │                          │  game thread)     │ │
                     │                          └────────┬─────────┘ │
                     └───────────────────────────────────┼───────────┘
                                                          │ HTTPS (libcurl/Boost.Beast)
                                                          │ webhook POST / REST
                                                          ▼
                                              ┌─────────────────────┐
                                              │   Discord REST API   │
                                              │  (webhooks, messages)│
                                              └──────────┬──────────┘
                                                          │
                                                          ▼
                                              ┌─────────────────────┐
                                              │    Discord channel    │
                                              │  (guild-specific text │
                                              │   channel per guild)  │
                                              └──────────┬──────────┘
                                                          │ user types in Discord
                                                          ▼
                                         ┌───────────────────────────────┐
                                         │  Sidecar bot process            │
                                         │  (DPP in C++, or discord.py /   │
                                         │   discord.js) — OR the module's │
                                         │  own Gateway client in-process  │
                                         │  (as kiwilabagency/mod-discord- │
                                         │  chat demonstrates it can be    │
                                         │  done, at the cost of owning a  │
                                         │  full gateway implementation)   │
                                         └────────────────┬────────────────┘
                                                           │ inject into game
                       ┌───────────────────────────────────▼─────────────────────────┐
                       │  Discord → game path: options, in order of simplicity         │
                       │  1. Shared MySQL table "mod_discord_relay_queue" — sidecar     │
                       │     bot INSERTs, worldserver polls on a WorldScript::OnUpdate  │
                       │     timer and calls Guild::BroadcastToGuild()                 │
                       │  2. AzerothCore SOAP interface (XML-RPC over HTTP) — sidecar   │
                       │     bot calls a `.send` GM/custom command via SOAP            │
                       │  3. Remote Access (RA) console (telnet-like) — sidecar bot     │
                       │     opens a socket and issues a command string                │
                       │  4. Direct in-process Gateway (no sidecar) — module owns the   │
                       │     Discord websocket itself, as mod-discord-chat does         │
                       └────────────────────────────────────────────────────────────────┘
```

### 3.2 Hook / API mapping to verified AzerothCore source symbols

All class/method names below were read directly from
`github.com/azerothcore/azerothcore-wotlk` (branch `master`) via `get_file_contents`; exact file
paths and line numbers are cited. Line numbers refer to the `master` branch at the time of this
research (2026-10-11) and may drift slightly with future commits — treat exact line numbers as a
point-in-time citation, not a permanent anchor.

**`PlayerScript`** — `src/server/game/Scripting/ScriptDefines/PlayerScript.h`
([source](https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Scripting/ScriptDefines/PlayerScript.h)):
- `virtual void OnPlayerLogin(Player* player)` — line 355. Use for guild-member "came online" notice.
- `virtual void OnPlayerBeforeLogout(Player* player)` — line 358, and
  `virtual void OnPlayerLogout(Player* player)` — line 361. Use for "went offline" notice (note: the
  player object/guild membership is still valid in `OnPlayerBeforeLogout`, which is generally the
  safer place to read guild info before teardown — **inferred** from naming/ordering, not explicitly
  documented in the header).
- `virtual void OnPlayerLevelChanged(Player* player, uint8 oldlevel)` — line 285. Use for
  level-up guild announcements.
- `virtual void OnPlayerAchievementComplete(Player* player, AchievementEntry const* achievement)` —
  line 407. Use for achievement announcements. (A `bool OnPlayerBeforeAchievementComplete` veto hook
  also exists at line 410, and `OnPlayerAchievementSave` at line 419.)
- `virtual void OnPlayerCreatureKill(Player* killer, Creature* killed)` — line 272. Use for boss-kill
  announcements; filter by `creature->IsDungeonBoss()` / `creature->GetCreatureTemplate()->rank`
  (exact boss-detection API not independently re-verified in this pass — **inferred** from common
  AzerothCore module conventions, e.g. mirrored by `kiwilabagency/mod-discord-chat`'s
  `IsBossKillEnabled` helper in `DiscordEvents.h`). Related: `OnPlayerPVPKill` (line 266),
  `OnPlayerCreatureKilledByPet`, `OnPlayerKilledByCreature`, `OnPlayerCreatureKillCredit` (line 282).
- `virtual void OnPlayerBeforeSendChatMessage(Player* player, uint32& type, uint32& lang, std::string& msg)`
  — line 341. **This is the hook for guild chat relay** — it fires for every outgoing chat message
  before it is sent; a module must check `type == CHAT_MSG_GUILD` (and optionally `CHAT_MSG_OFFICER`)
  to scope it to guild chat specifically, since `PlayerScript` has no guild-only chat hook.

**`GuildScript`** — `src/server/game/Scripting/ScriptDefines/GuildScript.h`
([source](https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Scripting/ScriptDefines/GuildScript.h)):
confirmed hooks (full text read): `OnAddMember(Guild*, Player*, uint8& plRank)`,
`OnRemoveMember(Guild*, Player*, bool isDisbanding, bool isKicked)`,
`OnMOTDChanged(Guild*, std::string const& newMotd)`, `OnInfoChanged(Guild*, std::string const& newInfo)`,
`OnCreate(Guild*, Player* leader, std::string const& name)`, `OnDisband(Guild*)`,
`OnMemberWitdrawMoney`/`OnMemberDepositMoney`/`OnItemMove` (guild bank events),
`OnEvent(Guild*, uint8 eventType, ObjectGuid::LowType playerGuid1, ObjectGuid::LowType playerGuid2, uint8 newRank)`
(fires for generic guild events, including **rank changes** — `newRank` parameter — making this the
hook to watch for "rank sync to Discord role" triggers), and `OnBankEvent`. These hooks are invoked
from `Guild.cpp` at verified call sites: `sScriptMgr->OnGuildAddMember` (`Guild.cpp:2315`),
`sScriptMgr->OnGuildRemoveMember` (`Guild.cpp:2361`), `sScriptMgr->OnGuildMOTDChanged` (`Guild.cpp:1296`),
`sScriptMgr->OnGuildInfoChanged` (`Guild.cpp:1320`), `sScriptMgr->OnGuildCreate` (`Guild.cpp:1110`),
`sScriptMgr->OnGuildDisband` (`Guild.cpp:1119`), `sScriptMgr->OnGuildEvent` (`Guild.cpp:2689`),
`sScriptMgr->OnGuildBankEvent` (`Guild.cpp:2711`).
**There is no `OnChat`/`OnGuildChat` hook in `GuildScript`** — confirmed by reading the full header;
guild chat text itself must be captured via `PlayerScript::OnPlayerBeforeSendChatMessage` as above, or
by reading `Guild::BroadcastToGuild` directly (see below).

**`WorldScript`** — `src/server/game/Scripting/ScriptDefines/WorldScript.h`
([source](https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Scripting/ScriptDefines/WorldScript.h)):
`OnStartup()`, `OnShutdown()`, `OnUpdate(uint32 diff)` (comment: *"Called on every world tick (don't
execute too heavy code here)"* — this is the explicit in-source warning that the module must not do
blocking work in this hook), `OnOpenStateChange(bool open)`, `OnAfterConfigLoad(bool reload)` (for
live `.reload config` support), and others. Use `OnStartup`/`OnShutdown` to start/stop the Discord
worker thread; use `OnUpdate` only to cheaply drain an already-populated outbound text queue into the
worker thread's lock-protected buffer (not to do network I/O directly).

**`AllCreatureScript`** — `src/server/game/Scripting/ScriptDefines/AllCreatureScript.h`
([source](https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Scripting/ScriptDefines/AllCreatureScript.h)):
confirmed hooks `OnAllCreatureUpdate(Creature*, uint32 diff)`, `OnCreatureAddWorld`,
`OnCreatureRemoveWorld`, gossip/quest hooks, and `GetCreatureAI`. **No boss-kill-specific hook exists
here** — boss-kill detection for Discord announcements should instead use
`PlayerScript::OnPlayerCreatureKill` (above) filtered by boss metadata, which is the pattern
`kiwilabagency/mod-discord-chat` also uses (`DiscordEvents::OnCreatureKill`).

**`CommandScript`** — `src/server/game/Scripting/ScriptDefines/CommandScript.h`
([source](https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Scripting/ScriptDefines/CommandScript.h)):
single pure-virtual method `GetCommands() const` returning
`std::vector<Acore::ChatCommands::ChatCommandBuilder>` — this is the pattern for a `.discordlink`
in-game command that generates a linking code (§3.4 below). Use this, not raw opcode handling, to
add a new slash-style `.discord link` chat command.

### 3.3 Discord→game direction: how guild chat is actually broadcast

Verified by reading `src/server/game/Guilds/Guild.cpp`
([source](https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Guilds/Guild.cpp)):

```cpp
// Guild.cpp:2175
void Guild::BroadcastToGuild(WorldSession* session, bool officerOnly, std::string_view msg, uint32 language) const
{
    if (session && session->GetPlayer() && _HasRankRight(session->GetPlayer(), officerOnly ? GR_RIGHT_OFFCHATSPEAK : GR_RIGHT_GCHATSPEAK))
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, officerOnly ? CHAT_MSG_OFFICER : CHAT_MSG_GUILD, Language(language), session->GetPlayer(), nullptr, msg);
        for (auto const& [guid, member] : m_members)
            if (Player* player = member.FindPlayer())
                if (_HasRankRight(player, officerOnly ? GR_RIGHT_OFFCHATLISTEN : GR_RIGHT_GCHATLISTEN) && !player->GetSocial()->HasIgnore(session->GetPlayer()->GetGUID()))
                    player->SendDirectMessage(&data);
    }
}
```

This is the **exact method to call** to inject a Discord-originated message into in-game guild chat.
It requires a `WorldSession*` whose player is actually a guild member with chat-speak rights — i.e.
to post as "a guild member," the module needs an in-world session (commonly done by using a
module-owned/GM "bot" account's session, analogous to how `mod-heimdall` holds a dedicated GM
session, per its README's "GM identity needs an account at gmlevel 1"). An alternative used by some
emulator modules is to build the `SMSG_MESSAGECHAT` packet directly via `ChatHandler::BuildChatPacket`
(verified: `src/server/game/Chat/Chat.cpp:273` and `:353`, two overloads — one taking raw
`ObjectGuid`s/strings, one taking `WorldObject*` sender/receiver) and send it via
`Guild::BroadcastPacketToRank`/`BroadcastPacket` (`Guild.cpp:2188`, `Guild.cpp:2196`) without
needing a live session at all — this is the cleaner approach for a "Discord→Guild" relay bot persona,
since it does not require simulating a logged-in character. `BuildChatPacket`'s packet layout
(verified, `Chat.cpp:273-351`) writes `SMSG_MESSAGECHAT` with `uint8 chatType`, `int32 language`,
sender GUID, a 0 flags field, then per-type sender/receiver name and GUID fields, then the message
length+text and a `chatTag` byte, confirming that `CHAT_MSG_GUILD` is a completely standard, unmodified
opcode shape — not a 3.3.5a-specific custom packet.

Incoming guild chat *from* the client is handled by
`WorldSession::HandleMessagechatOpcode` in `src/server/game/Handlers/ChatHandler.cpp:61`, which
explicitly dispatches on `case CHAT_MSG_GUILD` / `case CHAT_MSG_OFFICER` (verified at lines 136-137,
193-194, 234-235, 261-262) alongside every other standard chat type (`CHAT_MSG_SAY`, `CHAT_MSG_PARTY`,
`CHAT_MSG_RAID`, etc.) — confirming `CHAT_MSG_GUILD`/`CHAT_MSG_OFFICER` are not special-cased or
modified opcodes; they are handled by the exact same generic chat pipeline as every vanilla chat
channel, which is the basis for the §4 client-compatibility claim.

### 3.4 Account linking pattern

Use `CommandScript::GetCommands()` (§3.2) to add a `.discord link` command that:
1. Generates a short random code (e.g. 6 alphanumeric chars), stores
   `(account_id, code, created_at, expires_at)` in a new module table (e.g.
   `mod_discord_account_link`), and whispers the code back to the player via the existing
   `ChatHandler` reply mechanism (every `ChatCommandScript`/command handler in AzerothCore replies
   through `ChatHandler::SendSysMessage`/`PSendSysMessage`, the same pattern used throughout core
   command files such as `src/server/scripts/Commands/cs_*.cpp` — **inferred pattern**, not
   individually re-read line-by-line in this pass, but consistent across every AzerothCore
   `CommandScript` implementation).
2. The Discord side implements a `/link <code>` slash command (via the bot's application commands,
   not a Gateway message) that looks up the code in the shared table (direct MySQL read from the
   sidecar bot, or via a lightweight HTTP endpoint the module exposes) and on match writes
   `(discord_user_id, account_id, guild_id)` into a permanent `mod_discord_account_link` row, then
   deletes/expires the one-time code.
3. Guild-rank sync (`GuildScript::OnEvent` with `newRank`, confirmed `Guild.cpp:2689`) then looks up
   the account's linked Discord user ID and calls the Discord REST "Modify Guild Member" endpoint to
   add/remove the mapped Discord role — this call must happen from the sidecar/worker thread, not
   the game thread (§3.5).

### 3.5 Transport options and threading

- **Webhook POST (incoming to Discord only)**: simplest. A single outbound HTTPS POST per message to
  a per-guild webhook URL. No bot token needed for this direction, no Gateway connection, no intents.
  Rate limits apply the same as any other endpoint — see §3.6.
- **Full Gateway websocket (bidirectional, in-process)**: demonstrated viable in C++ by
  `kiwilabagency/mod-discord-chat`'s `DiscordGateway.cpp`, which implements its own Discord gateway
  client (hand-rolled, using Boost/OpenSSL already vendored by AzerothCore) rather than depending on
  an external library. This avoids a sidecar process entirely but means **owning gateway
  reconnect/resume/heartbeat logic** in the module — a non-trivial, ongoing maintenance burden.
- **DPP (brainboxdotcc/DPP)**: contrary to an assumption that "no mature C++ Discord gateway library
  exists," **DPP is in fact a mature, actively maintained C++ library**
  ([github.com/brainboxdotcc/DPP](https://github.com/brainboxdotcc/DPP)) supporting Discord API v10,
  sharding, slash commands, and voice, used in production by several public bots per its own README.
  It is **not header-only** (it's a compiled library with its own dependency set, including its own
  websocket/TLS and JSON handling) — integrating it into an AzerothCore module would mean vendoring
  or linking an additional, fairly large C++ dependency into the worldserver build, which is a real
  integration cost even though the library itself is solid. The two realistic choices are therefore:
  (a) hand-roll a minimal gateway client as `mod-discord-chat` did, accepting the maintenance burden,
  or (b) link DPP, accepting the build-dependency cost, or (c) avoid the Gateway entirely by using
  only outbound webhooks plus a **sidecar bot process** (Python `discord.py` or Node `discord.js`)
  that owns the Gateway connection and talks to the worldserver over a narrow channel (DB queue,
  AzerothCore's own SOAP interface, or Remote Access/RA console) — this is the lowest-risk path for
  a new module and is the approach recommended for an MVP (§7).
- **Threading**: the worldserver's single-threaded `World::Update(uint32 diff)` game loop is
  confirmed at `src/server/game/World/World.cpp:1131` — it runs the game simulation tick and
  explicitly must not block; the `WorldScript::OnUpdate` hook's own doc-comment says *"don't execute
  too heavy code here"* (verified in the header, §3.2). A Discord module must never perform
  synchronous network I/O from any hook that runs on the game thread (`PlayerScript`, `GuildScript`,
  `WorldScript::OnUpdate`, etc.). The correct pattern — used by both `Exitare/mod-discord-webhook`
  (`WebhookMgr`) and `kiwilabagency/mod-discord-chat` (`DiscordNetWorker.cpp`) — is: hooks push a
  plain data struct/string onto a thread-safe queue (mutex + `std::condition_variable`, or a
  lock-free SPSC queue), and a dedicated `std::thread` started in `WorldScript::OnStartup` and joined
  in `OnShutdown` drains the queue and performs all HTTP/websocket I/O.

### 3.6 Discord rate limits, intents, and token security (primary sources)

- **Rate limits**: Discord enforces both **per-route** and **global** limits, keyed by the
  authenticating bot token (or webhook ID/token for webhooks); exceeding a limit returns HTTP 429
  with a `retry_after` field and `X-RateLimit-*` response headers, and the docs explicitly warn
  *"rate limits should not be hard coded into your app"* — parse the response headers instead.
  ([discord rate-limits docs](https://discord.com/developers/docs/topics/rate-limits)). A guild chat
  relay under load (e.g., a busy guild) should therefore batch/coalesce messages and back off on 429
  rather than assuming a fixed messages-per-second budget.
- **Message Content Intent**: a bot that wants to read the `content` field of Gateway `MESSAGE_CREATE`
  events (needed to relay Discord→game text) must enable the privileged **`MESSAGE_CONTENT`** intent
  (bit `1 << 15`); without it, `content`/`embeds`/`attachments`/`components` arrive empty
  ([Gateway docs](https://discord.com/developers/docs/topics/gateway#message-content-intent), mirrored
  verbatim in the Message resource docs: *"An app will receive empty values in the content... fields...
  if they have not configured (or been approved for) the MESSAGE_CONTENT privileged intent"*). This
  matches what `mod-heimdall`'s own README independently warns installers about ("the bot needs
  Discord's Message Content intent or transcripts arrive with no words in them").
- **Bot token security**: Discord's own developer docs/portal treat the bot token as a full
  bearer-credential equivalent to a password for the application — store it only in local config
  (never commit it, never log it), and if ever exposed, regenerate it from the Developer Portal
  immediately. (General Discord developer-portal guidance; the OAuth2 topic page confirms the
  bot/application-credential model: [OAuth2 docs](https://discord.com/developers/docs/topics/oauth2).)
  Practically for this module: keep `Discord.Bot.Token` only in the module's `*.conf` (gitignored,
  mirroring the `.conf.dist`→`.conf` convention below), never in SQL or in any table the sidecar bot's
  own DB account can read if that account is scoped down, and prefer the sidecar-bot pattern (§3.5)
  specifically so the worldserver process itself never needs to hold a live Gateway session or
  broad `Manage Roles`/`Manage Webhooks` bot permissions in its own memory space.

### 3.7 Module layout and conf.dist convention (verified against `azerothcore/mod-solocraft`)

`azerothcore/mod-solocraft` ([repo root](https://github.com/azerothcore/mod-solocraft)) has **no
top-level `CMakeLists.txt`** — confirmed by directory listing (`.editorconfig`, `.gitattributes`,
`LICENSE`, `conf/`, `data/`, `include.sh`, `src/`). AzerothCore's modules build system auto-discovers
any directory dropped into `modules/` that contains a `src/` folder with a `*_loader.cpp` (here:
`src/SC_loader.cpp`) — individual modules do **not** need to write their own CMake build rules unless
they have unusual dependencies (as `kiwilabagency/mod-discord-chat` does, which *does* ship a
`mod-discord-chat.cmake` file to express its Boost/OpenSSL linkage — confirmed via its directory
listing). The `conf/` directory holds a `<modulename>.conf.dist` template that the operator copies to
`conf/<modulename>.conf` and edits (loaded through `sConfigMgr->GetOption<T>(...)`, as seen verified
in `Exitare/mod-discord-webhook`'s `server_hooks.cpp`: `sConfigMgr->GetOption<std::string>("Webhook.URL", "")`).
Recommended new-module layout, following this template:

```
mod-guild-discord/
├── conf/guild-discord.conf.dist      # Discord.Bot.Token, Discord.GuildMap.*, etc.
├── data/sql/db-characters/           # account-linking + outbound-queue tables
├── src/
│   ├── mod_guild_discord_loader.cpp  # Add*Scripts() aggregator, AzerothCore convention
│   ├── GuildDiscordPlayerScript.cpp  # OnPlayerLogin/Logout/LevelChanged/AchievementComplete/CreatureKill
│   ├── GuildDiscordGuildScript.cpp   # OnEvent (rank changes), OnAddMember/OnRemoveMember
│   ├── GuildDiscordWorldScript.cpp   # OnStartup/OnShutdown (worker thread lifecycle), OnUpdate (queue drain)
│   ├── GuildDiscordCommandScript.cpp # .discord link / .discord status (CommandScript::GetCommands)
│   ├── DiscordTransport.{h,cpp}      # webhook POST / REST client, rate-limit-aware
│   └── DiscordQueue.{h,cpp}          # thread-safe outbound queue
└── mod-guild-discord.cmake           # only if linking extra deps (e.g. DPP); not required for webhook-only MVP
```

### 3.8 Data model (proposed; not sourced from any existing module since none implements this feature set)

```sql
-- Links a realm account (not a specific character) to a Discord user, scoped to one Discord guild/server.
CREATE TABLE mod_discord_account_link (
    account_id      INT UNSIGNED NOT NULL,
    discord_guild_id BIGINT UNSIGNED NOT NULL,
    discord_user_id  BIGINT UNSIGNED NOT NULL,
    linked_at        DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (account_id, discord_guild_id)
);

-- One-time linking codes, short TTL.
CREATE TABLE mod_discord_link_code (
    account_id  INT UNSIGNED NOT NULL,
    code        CHAR(8) NOT NULL,
    created_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at  DATETIME NOT NULL,
    PRIMARY KEY (code)
);

-- Per-WoW-guild Discord channel/webhook mapping (one AzerothCore guild -> one Discord channel/webhook).
CREATE TABLE mod_discord_guild_map (
    wow_guild_id      INT UNSIGNED NOT NULL PRIMARY KEY,
    discord_guild_id  BIGINT UNSIGNED NOT NULL,
    discord_channel_id BIGINT UNSIGNED NOT NULL,
    webhook_url       VARCHAR(255) NOT NULL,
    rank_role_json    TEXT NULL -- maps WoW guild rank id -> Discord role id
);

-- Outbound relay queue if using the DB-polling Discord->game path (sidecar bot writes, worldserver reads).
CREATE TABLE mod_discord_relay_queue (
    id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    wow_guild_id INT UNSIGNED NOT NULL,
    discord_user_id BIGINT UNSIGNED NOT NULL,
    display_name VARCHAR(64) NOT NULL,
    message     VARCHAR(2000) NOT NULL,
    created_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    processed   TINYINT(1) NOT NULL DEFAULT 0
);
```

### 3.9 Deployment notes

- Build with modules enabled: `cmake .. -DMODULES=static` (or dynamic), matching the pattern shown
  in `kiwilabagency/mod-discord-chat`'s installation README.
- Apply the DB SQL migration(s) under `data/sql/db-characters/` (AzerothCore's DB updater applies
  these automatically on next worldserver start if the updater is enabled, per the same README's
  documented convention).
- Copy `conf/guild-discord.conf.dist` → `configs/modules/guild-discord.conf`, fill in
  `Discord.Bot.Token` (sidecar bot) and/or per-guild webhook URLs.
- If using the sidecar-bot pattern: run it as a separate systemd unit / container, with its own
  **scoped** MySQL account limited to this module's tables (mirroring `mod-heimdall`'s stated design:
  "the bot's database account reaches only Heimdall's own database").
- Enable required privileged intents (`SERVER MEMBERS`, `MESSAGE CONTENT`) in the Discord Developer
  Portal's Bot page **and** in the module/bot config — both must agree or Discord rejects the Gateway
  identify payload (per `mod-discord-chat`'s own install docs, consistent with Discord's own intent
  docs cited in §3.6).

---

## 4. 3.3.5a client compatibility notes

- **No client modification needed for the core bridge.** Guild chat is relayed using the completely
  standard `SMSG_MESSAGECHAT` opcode with `CHAT_MSG_GUILD`/`CHAT_MSG_OFFICER` message types,
  confirmed unmodified in both the outbound path (`ChatHandler::BuildChatPacket`,
  `Chat.cpp:273-351`) and inbound path (`WorldSession::HandleMessagechatOpcode`,
  `ChatHandler.cpp:61`, with explicit `case CHAT_MSG_GUILD` handling at lines 136, 193, 234, 261) —
  this is the same opcode and handler every retail-equivalent 3.3.5a client already understands; a
  Discord-originated guild message injected via `Guild::BroadcastToGuild`/`BroadcastPacket` is
  indistinguishable, at the protocol level, from a normal player's guild chat message.
- **Addon-channel limitations (relevant only if a custom in-game UI/overlay is also wanted):**
  the client's `SendAddonMessage` API (3.3.5a-era global function, later moved to
  `C_ChatInfo.SendAddonMessage` in retail) has a hard **16-character prefix limit** and a
  **255-character message limit** ("All characters (decimal ID 1-255) are permissible except NULL"),
  per the community API reference
  ([warcraft.wiki.gg API:C_ChatInfo.SendAddonMessage](https://warcraft.wiki.gg/wiki/API:C_ChatInfo.SendAddonMessage),
  historically known as `API_SendAddonMessage`/`SendAddonMessage`, added in patch 1.12.0 per the same
  page's patch-history section). There is also a **server-side throttle** (10-message burst allowance
  per registered prefix, refilling at 1/sec, per the same page) — relevant only if the module also
  ships a FrameXML addon talking to the server over `CHAT_MSG_ADDON`; it is **not** needed for the
  core guild-chat-relay feature, which rides on ordinary chat packets instead.
- **No HTTP from the WoW 3.3.5a client.** The client's embedded Lua 5.1 sandbox (FrameXML-era) has no
  socket or HTTP API exposed to addons — this is long-standing, widely known addon-development
  common knowledge rather than a single citable spec page; it is the reason every "WoW↔web" bridge
  (including this one) must go through the server, not the client. (No single authoritative primary
  source was found/cited for this specific negative claim beyond the absence of any such API in the
  addon API reference wikis — **treat as community consensus / inferred**, not independently
  footnoted here.)
- **Unicode/UTF-8**: Discord messages are full UTF-8. The 3.3.5a client's chat rendering uses the
  game's own font/glyph tables, and many non-Latin or emoji glyphs will not render in the default
  client fonts — practical mitigation: strip or transliterate Discord emoji/unsupported glyphs before
  relaying into guild chat (not independently verified against a specific client glyph-table source
  — **inferred/practical**, consistent with widespread AzerothCore-community experience, not formally
  cited).
- **Discord/WoW message-length mismatch**: Discord's message `content` field has a documented
  **2000-character** limit on normal (non-Nitro) `Create Message` calls
  ([Discord message resource docs](https://discord.com/developers/docs/resources/channel#create-message),
  current canonical location `https://discord.com/developers/docs/resources/message#create-message`),
  versus WoW 3.3.5a chat's effective server-side cap, which the same community addon-API
  documentation states as 255 bytes for `SendAddonMessage` payloads and which `mod-heimdall`'s own
  operational notes independently confirm in practice ("Replies reach players as whispers, in
  240-byte segments. Longer replies are split on word boundaries."). A Discord→guild-chat relay must
  therefore **split** long Discord messages across multiple in-game chat lines.
- **Item/spell hyperlinks**: 3.3.5a chat hyperlinks use the format
  `|cFFxxxxxx|Htype:payload|h[text]|h|r` (color code, `|H` hyperlink-open marker, colon-delimited
  type:payload, `|h` payload-end marker, bracketed display text, second `|h`, `|r` color-reset),
  documented on the community API wiki's Hyperlinks page
  ([warcraft.wiki.gg Hyperlinks](https://warcraft.wiki.gg/wiki/Hyperlinks)). For Discord, these
  control-character-laden strings should be parsed out and converted to plain display text (optionally
  with a Wowhead-style link built from the extracted item/spell ID) rather than relayed verbatim,
  since Discord has no concept of these escape sequences and would show the raw `|c...|H...|h` noise.
- **Faction/language**: WotLK guilds are single-faction by design (a guild cannot contain both Horde
  and Alliance characters), so the classic "Horde can't read Alliance chat" language-barrier mechanic
  (general WoW knowledge; not individually footnoted here) is a non-issue for a guild-scoped bridge —
  it would only matter if the module were extended to bridge cross-faction/global chat instead
  (as `kiwilabagency/mod-discord-chat`'s realm-wide bridge does, where it explicitly shows
  "[faction icon]" per message to disambiguate, per its own README snippet).
- **MPQ/client patching**: only required if a custom FrameXML addon or client-side UI is shipped
  alongside the bridge (e.g., an in-game Discord-status frame) — not required for the core
  server-side guild-chat relay feature, which needs zero client changes.
- **Eluna as an alternative implementation path**: `ElunaLuaEngine/Eluna` (and AzerothCore's
  integration via `mod-eluna-engine`) exposes a Lua hook system that could implement much of this
  without recompiling the worldserver — hot-reloadable scripts, faster iteration, and HTTP calls from
  Lua via Eluna's available socket/HTTP extensions in some builds (**unverified** — Eluna's HTTP
  capability varies by fork/build and was not independently confirmed against a specific Eluna source
  file in this pass). Trade-off: Eluna's hook surface does not necessarily expose every `GuildScript`
  granularity 1:1 (e.g., bank-event hooks) and adds a second scripting runtime/dependency to operate
  and secure; for a production guild-chat bridge with account linking and rank-role sync, the
  C++-native `ScriptObject` approach documented in §3 is the safer, more directly verifiable path,
  while Eluna remains a reasonable prototyping/iteration option for the chat-relay subset specifically.

---

## 5. Risks

- **Owning a Discord Gateway client in C++ is a real ongoing maintenance burden** (reconnect/resume,
  heartbeat ack tracking, intent/version bumps) if the in-process approach is chosen instead of a
  sidecar bot — demonstrated as *possible* by `mod-discord-chat` but not something to underestimate.
- **Blocking the world thread** is the single highest-severity implementation risk; any accidental
  synchronous network call from a `PlayerScript`/`GuildScript` hook or from `WorldScript::OnUpdate`
  will stall the entire realm for every player, not just Discord traffic.
- **Token/credential exposure**: a compromised bot token with `Manage Roles`/`Manage Webhooks`
  permissions could be used to impersonate the guild's Discord presence; scope bot permissions as
  tightly as possible and never let the game-account-linking table leak into a context the bot
  itself can read if that can be avoided (mirroring `mod-heimdall`'s explicit security design).
- **Rate-limit storms** from a busy guild (raid wipes spamming boss-kill-attempt chat, achievement
  spam) hitting Discord's webhook/REST rate limits — must coalesce/throttle at the source, not rely
  on Discord's 429 responses alone.
- **License ambiguity in the closest prior-art module** (`kiwilabagency/mod-discord-chat`'s GitHub
  API-reported license is `NOASSERTION`/"Other" despite an AGPL header in the raw `LICENSE` text) —
  confirm licensing terms carefully before reusing any of its code rather than only implementing a
  compatible architecture independently.
- **Unverifiable catalogue claim**: because `azerothcore.org/catalogue.html` could not be scraped
  (client-rendered), this report cannot certify that it is the complete up-to-date list of all
  AzerothCore modules; the GitHub topic/search results used here are believed to be a reasonable
  proxy but are **not a verified 1:1 match to that catalogue**.

---

## 6. Suggested MVP / phased plan

1. **Phase 0 — Outbound-only guild chat relay (webhook POST, no Gateway, no linking).**
   Implement `GuildDiscordPlayerScript::OnPlayerBeforeSendChatMessage` filtering `CHAT_MSG_GUILD`, a
   thread-safe queue, a worker thread (`WorldScript::OnStartup`/`OnShutdown`), and a plain HTTPS POST
   to one webhook URL per WoW guild (`mod_discord_guild_map`). No Discord→game direction yet. This
   alone already beats every existing module found except `mod-discord-chat` for guild-scoping, since
   none of the examined repos scope by individual WoW guild.
2. **Phase 1 — Online/offline + level/achievement/boss-kill announcements.** Wire
   `OnPlayerLogin`/`OnPlayerBeforeLogout`, `OnPlayerLevelChanged`, `OnPlayerAchievementComplete`,
   `OnPlayerCreatureKill` (filtered for boss/dungeon kills) into the same outbound queue, each tagged
   with the player's guild (via `Player::GetGuildId()`/guild lookup — not independently re-verified
   method name in this pass, **inferred** from standard AzerothCore `Player` API conventions).
3. **Phase 2 — Discord→game relay via sidecar bot + DB queue or SOAP.** Stand up a minimal
   `discord.py`/`discord.js`/DPP sidecar that owns the Gateway connection (so the worldserver never
   needs privileged intents in-process), listens in the mapped channel, and either (a) writes to
   `mod_discord_relay_queue` for the worldserver to poll on a `WorldScript::OnUpdate` timer and call
   `Guild::BroadcastToGuild`/`BroadcastPacket`, or (b) calls AzerothCore's SOAP interface to invoke a
   custom `.discordrelay` GM command that does the same — SOAP is the less-custom, already-existing
   core interface and is the recommended starting point over inventing a bespoke socket protocol.
4. **Phase 3 — Account linking.** `.discord link` in-game `CommandScript` + Discord `/link` slash
   command against `mod_discord_account_link`/`mod_discord_link_code` (§3.4).
5. **Phase 4 — Guild rank → Discord role sync.** Subscribe to `GuildScript::OnEvent` (rank-change
   events, confirmed hook at `Guild.cpp:2689`) and push a Discord REST "modify guild member roles"
   call from the sidecar bot for any linked account.

---

## 7. Sources (deduplicated)

**AzerothCore source (primary):**
- https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Scripting/ScriptDefines/PlayerScript.h
- https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Scripting/ScriptDefines/GuildScript.h
- https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Scripting/ScriptDefines/WorldScript.h
- https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Scripting/ScriptDefines/AllCreatureScript.h
- https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Scripting/ScriptDefines/CommandScript.h
- https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Guilds/Guild.cpp
- https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Chat/Chat.cpp
- https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/Handlers/ChatHandler.cpp
- https://github.com/azerothcore/azerothcore-wotlk/blob/master/src/server/game/World/World.cpp
- https://github.com/azerothcore/mod-solocraft

**Prior-art modules examined:**
- https://github.com/kiwilabagency/mod-discord-chat
- https://github.com/Calmoran/mod-heimdall
- https://github.com/Exitare/mod-discord-webhook
- https://github.com/Diabloxx/mod-gm-discord (metadata only, not deep-dived)
- https://github.com/quadseven/mod-overseer (metadata only, not deep-dived)
- https://github.com/Griznah/azerothcore-discord (metadata only, not deep-dived)
- https://github.com/brainboxdotcc/DPP

**AzerothCore project docs:**
- https://www.azerothcore.org/catalogue.html (unreadable/client-rendered; listed for completeness)
- https://www.azerothcore.org/wiki/agentic-engineering (referenced via mod-heimdall README)

**Discord Developer Portal (primary):**
- https://discord.com/developers/docs/topics/rate-limits
- https://discord.com/developers/docs/topics/gateway#message-content-intent (current canonical URL
  resolves to https://docs.discord.com/developers/events/gateway#message-content-intent)
- https://discord.com/developers/docs/resources/webhook#execute-webhook (current canonical URL
  resolves to https://docs.discord.com/developers/resources/webhook#execute-webhook)
- https://discord.com/developers/docs/resources/channel#create-message (current canonical URL
  resolves to https://docs.discord.com/developers/resources/message#create-message)
- https://discord.com/developers/docs/topics/oauth2

**Community/wiki API references (secondary, used for client-side facts not covered by Blizzard
primary docs, which are not publicly republished):**
- https://warcraft.wiki.gg/wiki/API:C_ChatInfo.SendAddonMessage (redirect target of the historical
  `API_SendAddonMessage` page)
- https://warcraft.wiki.gg/wiki/Hyperlinks

---

## Labeling key used throughout this report
- **Verified**: confirmed by directly reading the cited primary source file/page in this research pass.
- **Inferred**: a reasonable conclusion drawn from verified facts or common/standard patterns, but not
  itself directly read from a primary source in this pass.
- **Unverified**: a claim from search-result metadata, a README description, or general community
  knowledge that could not be directly confirmed against source/primary docs in this pass.
