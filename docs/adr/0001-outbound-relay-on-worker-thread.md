# Outbound relay uses a worker thread and a bounded queue

Script hooks only enqueue messages on a bounded Relay Queue. A dedicated worker thread, started in `WorldScript::OnStartup` and stopped in `OnShutdown`, does all HTTPS to Discord using Boost.Beast and OpenSSL, which AzerothCore already vendors. Blocking the world thread would stall every player on the realm, so no network I/O happens in any hook or in `OnUpdate`.

## Consequences

- When the queue is full the oldest message is dropped. Losing chat lines is preferred over unbounded memory or backpressure on the game thread.
- The worker honours Discord `429`/`Retry-After` and `X-RateLimit-*` headers and retries other failures only a couple of times.
- libcurl was rejected to avoid a new build dependency.
