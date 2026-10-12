## Agent skills

### Issue tracker

Issues live in GitHub Issues (via the `gh` CLI). See `docs/agents/issue-tracker.md`.

### Triage labels

Default five-label vocabulary (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` + `docs/adr/` at the repo root. See `docs/agents/domain.md`.

### Core source

The AzerothCore core isn't checked out locally; only CI compiles `src/GuildDiscord.cpp`. Read core headers from `azerothcore/azerothcore-wotlk` on GitHub: `src/server/game/Scripting/ScriptDefines/*.h` (hooks), `src/common/Configuration/ConfigValueCache.h`. Keep logic that doesn't need the core in standalone headers covered by `tests/relay_tests.cpp`. Search the repo or those paths only, never `/`.
