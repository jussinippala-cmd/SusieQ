# CoLight persistent BLE connection + cached state — design

## Problem

The CoLight BLE bridge (`docs/superpowers/plans/2026-07-05-colight-ble-bridge.md`) was built on
a "connect-on-demand" model: each `GET /colight/state` or `POST /colight` call opens a fresh BLE
connection to the switch panel, subscribes to its notify characteristic, waits up to 3s for a
state frame (`0xf9 ...`), then disconnects. This was live-tested on the boat on 2026-07-05 and
fails consistently: `/colight/state` returns `{"success":false,"error":"no_state_notification"}`
on every call.

Root cause, confirmed with a direct BLE monitor (`tools/colight-ble/colight_ble.py monitor`)
against the physical panel:

- Subscribing and waiting ~60s with no physical switch touched produced **zero** notifications.
- The instant a physical switch was toggled, a full notification burst (`f1`,`f2`,`f3`,`f4`,`f9`)
  arrived within ~150ms, and another burst arrived when the switch was toggled back.

**The panel only reports state on a physical change event — it does not proactively report state
on connect/subscribe, and it does not respond to any periodic or on-demand poll.** The June 2026
finding that it "sends frames continuously every ~2-5s" was an artifact of that session's tester
pressing all 12 switches in sequence, not an idle heartbeat.

This breaks both HTTP endpoints, since both share the same `colight_run()` code path:
- `GET /colight/state` (read-only) can never succeed unless a human happens to touch a switch
  during the 3s window of that specific HTTP call.
- `POST /colight` (write) needs a freshly-read "before" frame to safely flip only the target
  channel's bit (writes are full 6-byte state snapshots, not deltas — confirmed 2026-07-05: writing
  channel 6 on without channel 5's bit turned channel 5 off). Without a readable baseline, writes
  can't be built safely either.

Separately (not part of this design, tracked for awareness): the GL-XE300 relay daemon
(`susieq-colight.sh` / `/etc/init.d/susieq-colight`) from that same plan's Task 5 has never been
deployed to the modem (confirmed via SSH 2026-07-05 — file doesn't exist). Even with this fix,
the website won't show live CoLight state until that daemon is installed. Out of scope here per
explicit user decision.

## Solution

Replace the connect-per-call model in `susieq_dashboard/src/colight.cpp` with a persistent
connection maintained by a dedicated FreeRTOS task, backed by a mutex-guarded in-RAM cache that
the HTTP handlers read/write instead of talking to BLE directly.

### Architecture

- **`colight_task`** (new FreeRTOS task, created once from `colight_init()`): owns a single
  `NimBLEClient*` for the panel's whole lifetime. On start (and after any disconnect), it scans
  for `MG-P1C-12P`, connects, discovers the service/characteristic, and subscribes to notify.
  If any step fails, it waits ~5s and retries, forever. This task is the only code that ever
  calls `victron_pause_scan()`/`victron_resume_scan()`, and only around the scan+connect step —
  not around every read/write, since those no longer need a fresh connection.
- **Shared cache** (mutex-protected struct): `{ bool valid; uint8_t frame[6]; uint32_t
  last_updated_ms; bool connected; }`. Updated in two places: the notify callback (every real
  `f9` frame, from a physical switch or the panel's own echo of our write) and, optimistically,
  right after a successful write (see below).
- **HTTP handlers** (`main.cpp`, `/colight` and `/colight/state`): no longer call into BLE at
  all. They take the cache mutex, read or update `frame`, release it, and respond immediately.
  `POST /colight` performs the actual `chr->writeValue()` using the *same already-open connection*
  owned by `colight_task` (guarded by the same mutex so the task doesn't concurrently touch the
  client mid-write).

### Data flow

1. **Physical switch touched:** panel sends `f9` burst → `colight_task`'s notify callback updates
   cache (`valid=true`, new `frame`, `last_updated_ms=millis()`) → next `GET /colight/state` call
   sees it immediately (no BLE wait in the HTTP path at all).
2. **`GET /colight/state`:** reads cache under the mutex. `valid==false` →
   `{"success":false,"error":"unknown_state"}`. Otherwise returns the decoded 12-channel state
   from `frame`, plus `connected` and `last_updated_ms` so callers (the GL-XE300 daemon, later)
   can judge staleness.
3. **`POST /colight?channel=N&action=on|off`:** if cache `valid==false` or `connected==false` →
   `{"success":false,"error":"not_ready"}`. Otherwise: take mutex, copy cached `frame`, flip the
   target channel's bit per the existing `colight_protocol` logic, prepend `0xf5`, write 7 bytes
   over the existing connection, optimistically store the new frame in the cache with a fresh
   `last_updated_ms`, release mutex, respond `success:true`. The panel's own subsequent `f9` echo
   (if any) will reconcile the cache moments later via the normal notify path — no separate
   confirmation step is needed synchronously.

### Victron coexistence

`victron_pause_scan()`/`victron_resume_scan()` now bracket only the rare scan+connect step inside
`colight_task` (at boot and after any disconnect), not every read/write. NimBLE is expected to
support one active GATT client connection concurrently with an ongoing passive scan (standard BLE
central capability) — this must be verified live on the boat once implemented: confirm Victron
battery data keeps updating continuously while the CoLight connection stays open indefinitely,
not just immediately after connecting.

### Error handling

- **BLE disconnect** (panel out of range, power cycle, etc.): `colight_task`'s disconnect
  callback marks cache `connected=false` (keeps the last known `frame`/`valid` — a recent-but-now
  possibly-stale reading is more useful than discarding it) and the task's retry loop reconnects
  in the background. `last_updated_ms` lets a future caller (the GL-XE300 daemon) decide whether
  a `connected=false` cached reading is still trustworthy.
- **Cold start:** `valid=false` until the first real `f9` arrives (from a physical touch or our
  own write) — accepted limitation per user decision, no attempt to reverse-engineer a
  panel-side "query state" command right now.
- **Write attempted with no known baseline:** rejected with `not_ready` rather than guessing/
  writing an unsafe frame that could clobber other channels.
- **`chr->writeValue()` itself returns false** (e.g. connection dropped between the `connected`
  check and the write): respond `{"success":false,"error":"write_failed"}` and do *not* update
  the cache — leave the last known-good frame in place rather than caching an unconfirmed guess.

### Testing

- `colight_protocol.cpp`'s pure bit-manipulation logic is unchanged and already covered by native
  Unity tests (`susieq_dashboard/test/test_colight_protocol/`) — no changes needed there.
- The new task/cache/BLE-connection code depends on NimBLE and can't run in the native test
  environment. Verification is manual, on the boat, after flashing:
  1. Physical switch toggle shows up in `/colight/state` within ~1s (was: never, without perfect
     3s-window luck).
  2. A website/`POST /colight` command flips the correct physical channel without clobbering
     others (repeat the existing two-channel non-interference check from the original plan's
     Task 6).
  3. Victron `/data` battery/solar fields keep updating normally for several minutes with the
     CoLight connection held open the whole time (not just right after boot).
  4. Deliberately move the panel out of BLE range (or power off/on) and confirm `colight_task`
     reconnects automatically within a reasonable window (a few retry cycles), and that
     `/colight/state`'s `connected` field reflects the drop.

## Out of scope

- GL-XE300 relay daemon deployment (`susieq-colight.sh`) — known separately missing, explicitly
  deferred by the user in this session.
- Reverse-engineering a panel "query current state" BLE command — explicitly deferred in favor of
  the "unknown until first touch" cold-start behavior.
