# Internet play roadmap

The main use case is two Windows PCs on different Internet connections, playing through the existing TLS relay at `gbfbc.com:8443`. Each player runs their own game. Progress must not depend on the other player finishing a battle or on a perfect connection.

This is a plan, not a list of shipped features. Keep the current player kit available while each milestone is developed and tested.

## Progress on October 7, 2026

Session timing counters and a private diagnostic export are available. They
measure relay RTT, queued changes, game acknowledgements, transfer duration,
busy intervals and browser refreshes. Event-level capture tracing and the
separate-PC acceptance test remain outstanding.

Protocol 7 now exchanges changed rows against a shared completed baseline and
sends sparse merged proposals. Native sanitizer tests cover matching and
missing baselines, 21 differential disconnect cases, invalid maps and cache
allocation failure. Two mGBA games with delayed, fragmented TCP confirm a
one-row transfer for one death in an 88-pair registry. The same native integration
suite passes through the deployed public TLS relay, with both clients running
on one PC. This is not the separate-connection acceptance test.
Routine registry readings now use a background path and locally validated
application. Accepted team changes retain their native common readiness
barriers. Separate-PC acceptance and whole-frame pause budgets remain to qualify.

Death recovery records now use bounded, atomic local writes with room and role
scope, strict validation and a corruption checksum. Unchanged reports do not
rewrite the disk. Pending reports are resent immediately after reconnection;
write failures retain the event in memory and retry at most every five seconds.
Automated restart tests cover simulated 5-, 30- and 300-second outages. This is
part of milestone 4, not completion: capture durability across unsaved game
termination remains open.

Approved team changes now retain a bounded local recovery record. A command ACK
does not confirm the team: both real party readings, selections and locks must
match. Restart and reconnect replay the same proposal through the existing
relay. A matching saved agreement reuses the previous vote; a missing agreement
requires another manual vote. Changed pairs, participants or rules cancel the
old agreement. Unit tests cover one-sided commits and fresh companions/relay;
the original public TLS test used synthetic game readings. Forced A-first and
B-first native commits now also pass through public TLS with two actual ROMs
on one PC, closing both TCP connections and replacing both companion instances.
Saved approval is reused; partial parties remain unconfirmed until both game
readings match. Two separate remote PCs still need qualification.

Local registry application now has an isolated compiled-ROM cycle benchmark.
On identical metadata fixtures, a single capture with 88 pairs fell from
452.974/424.238 ms (A/B) to 75.248/98.745 ms. A linked-death registry update took
98.741 ms on B and 74.821 ms for A's confirmation. A 57-capture catch-up at 87
pairs still reached 112.819 ms. Six-pair cases are about 4.5 ms, slightly above
the former 4.1 ms; one-pair maximum rose from 0.621 to 1.576 ms. The new exact
duplicate indexes trade that small fixed cost for much shorter large-registry
validation. Ten thousand differential validator cases and forced 88-way hash
collisions preserve the prior acceptance rules.

These are emulated CPU-time estimates for an isolated routine with IRQs
disabled, not WAN latency or measured visible frame pauses. Death cleanup,
team movement, Lua, browser refresh and the rest of the frame are excluded.
The 100 ms ordinary-change target passes these fixtures only; bulk catch-up,
arbitrary identities, other game work and slow host PCs are not certified.

A further indexed-reconciliation pass lowers those same 88-pair single-capture
costs from 75.248/98.745 ms to 40.547/42.187 ms (A/B). Death application and
confirmation take 42.059/40.122 ms. The tested 57-capture catch-up falls from
112.819 to 79.017 ms; one-, six- and thirty-pair sample maxima are 1.107, 3.172
and 18.402 ms. These are still isolated CPU measurements, not a guarantee of
end-to-end Internet latency or frame pacing. All 42 fixtures now fall below
100 ms, but whole-frame work and arbitrary inputs remain outside that claim.

Exact-key indexes replace repeated merge lookups. The immediate background
path validates preparation and ownership once, then writes in that same
synchronous call; staged protocol commits still revalidate. Four thousand
comparisons against the previous merge implementation and four thousand
byte-exact staged-versus-synchronous save comparisons supplement the existing
validator and sanitizer tests. Fixture stack depth rises from 900 to 1680
bytes and remains within the harness's 2048-byte guard. Actual game call-chain
stack budgets remain to qualify.

## Where we are

Routine background application is now frame-sliced. Exact local snapshot
comparisons cancel stale work before it can write. Forty-two isolated ROM
fixtures have a largest individual callback of 7.928 ms, versus 40–42 ms for
the previous 88-pair synchronous application and 79 ms for bulk catch-up.
At 88 pairs the longest fixture uses 35 callbacks (setup plus 34 subsequent
game frames, about 0.57 seconds at normal speed); the 57-capture catch-up uses
49. The game keeps running between callbacks. This improves the largest
single burst, not total CPU work: the 88-pair maximum totals 131.068 ms and
bulk catch-up 159.239 ms. Small-registry maxima are 4.739 ms for one pair and
3.870 ms for six. These costs still exclude the rest of the frame and WAN
transport, and do not certify a visible-pause or end-to-end latency budget.

The sliced path matches the staged path byte-for-byte in 4000 cases. Native
sanitizers also check 10000 incremental validator cases, 476 mutation
interruptions and every row in all seven save storage banks. No unfinished
step writes save data. Full frame pacing and real separate-PC qualification
remain open.

Actual-body fixtures now include six party members, 390/420 boxed Pokémon and
88 pair records for both roles. Checking unencrypted identities before species
reduces the worst measured six-member preparation from 1474.525 to 377.408 ms
and death cleanup from 247.551 to 65.914 ms. The final team write stays around
16.197 ms. These are isolated routine costs with IRQs disabled. Dense-PC team
preparation remains synchronous; full frame pacing still needs measurement.

Reusing the PC slots freed by boxed selections before searching for other empty
slots lowers that same six-body preparation maximum again, to 186.330 ms.
Mixed and unchanged-party fixtures check read-only preparation and full-PC
refusal when outgoing members need more space than the selections free.
This is still an isolated synchronous routine measurement.

Direct unencrypted identity reads and per-box address reuse lower that
preparation maximum again, to 139.954 ms from 186.330 ms. The same tests pass
on both ROM builds, including duplicate-body refusal, identities sharing only
one key, full-PC diagnostics and preservation of displaced members. Whole-frame
pacing and separate-PC Internet acceptance remain outstanding.

The shared local identity search now walks each contiguous save bank directly,
keeping the original first-match order. Sanitizers check 46992 hit/miss queries
across all counts and both roles, including unchanged save bytes and untouched
outputs on misses. The same actual-body ROM fixtures now peak at 114.882 ms for
team preparation and 52.716 ms for death cleanup, versus 139.954 and 65.914 ms.
No cache persists across frames or save loads. These measurements still exclude
whole-frame work and Internet transport.

Lazy selected-pair reads now visit each registry row at most once during a team
preparation call, preserving refusal order. Six-box swap cost falls from
114.881 to 106.093 ms on identical actual-ROM fixtures. Newly timed narrow
compositions still reach 208.128 ms with dense/full PCs; searching deposit
vacancies is the next identified cost. Small paths show sub-millisecond
differences in either direction, rather than a universal improvement. No cache
survives the call. Actual production stack budgets, whole frames and separate-PC
qualification remain open.

- Local game readings arrive every 15 frames, about 250 ms at normal speed. New readings wake the coordinator. The retry guard is one second.
- Routine registry updates apply locally in the background. Accepted team changes still need both games to be available and can pause movement during their native exchange. Death reports use a separate path and wait for the receiving game to be available.
- This PC measured a median relay round trip of 24.6 ms and a 95th percentile of 39.8 ms over 20 samples on October 7, 2026. That does not describe another player's connection.
- Two emulator cores behind a delayed TCP proxy completed a small exchange in about 2.27 seconds and an 88-pair recovery in 6.08 seconds. Those are simulated network tests, not a test between two remote PCs. The earlier small-exchange measurement was 2.17 seconds: recent changes reduced scheduling waits, not measured transfer time.

## 1. Measure the whole trip

Record when a game change is detected, queued, sent, received, applied and displayed. Measure relay round trips, bytes transferred, reconnects and time spent waiting for a game to be available. Use local monotonic timers and acknowledgements; do not subtract timestamps from unrelated PC clocks.

Show a simple connection status in the companion. Diagnostic exports must omit room codes, session URLs and other connection secrets. Keep detailed logging bounded and optional.

**Done when:** two players on separate Internet connections can reproduce a slow update and the report identifies whether the delay came from the network, scheduling, a busy game or the page. Record medians and 95th percentiles for captures, deaths and accepted team changes before setting final performance budgets.

## 2. Send changes instead of the whole registry

Send changed capture and pair records against a version the receiver has acknowledged. Preserve terminal death and failed-encounter history. Check the resulting state and use a complete exchange when a baseline is missing or the two games disagree.

Version the protocol and negotiate support before using the new path. Keep the current full exchange as a fallback. A repeated message must not create another pair, apply a proposal twice or revive a dead Pokémon.

**Done when:** a single capture transfers only the required changes even with 88 existing pairs, produces the same final state as a full exchange, and survives disconnects at every acknowledgement boundary. Compare bytes and elapsed time with milestone 1's baseline on real Internet connections.

Proposed target: with each player's relay round trip at or below 100 ms and both games available, 95% of single-capture updates appear within two seconds of local detection. Validate this target before making it a player-facing promise.

The acknowledged-baseline approach is described in [Glenn Fiedler's Snapshot Compression](https://gafferongames.com/post/snapshot_compression/). Its relevance here is sending fewer unchanged records; this project does not need a real-time physics protocol.

## 3. Keep network waiting out of gameplay

Exchange and validate pending changes in the background. Apply them locally at a safe point, without holding movement while waiting for a WAN response. Prioritize death notices over routine refreshes. A busy partner should delay their own application, not the other player's exploration.

Team composition needs separate handling: keep an accepted proposal pending until both games confirm it. Define recovery for a disconnect between the two local commits so the companion cannot report a shared team that only one game has applied. Do not assume the two PCs can commit at exactly the same instant.

**Done when:** one player can remain in battle while the other walks, captures and receives queued updates. Network delay cannot extend a local movement lock. Measure local commit pauses; an initial target is below 100 ms for ordinary changes, subject to native ROM validation. The page distinguishes received changes from changes applied in-game.

## 4. Recover without losing events

Add event IDs, acknowledgements and a bounded recovery journal where the current reconnect path cannot recover a change. Persist enough progress to survive client and VPS restarts. Scope events to the room, run and participant; reject stale or mismatched events before applying them.

Set explicit journal retention and storage limits. Beyond the retained history, recover from validated full game state rather than silently dropping changes. Save files stay on players' PCs. Neither the relay nor a quick load may reverse terminal deaths.

**Done when:** a capture or death made just before a disconnect survives reconnecting, repeated delivery, closing the launcher and restarting the relay. Test outages of 5 seconds, 30 seconds and 5 minutes, simultaneous reconnects, and a partner restarting or disabling standalone Nuzlocke after defeat. Reconnection must not require another room code for a still-valid room.

## 5. Make the launcher and companion easier to trust

Use plain status messages: partner fighting, update waiting, reconnecting, or incompatible version. Show the last confirmed update and make pending team votes obvious. Add a one-click diagnostic export using the redaction rules from milestone 1.

Check compatible ROM, bridge and companion versions before joining. Provide a verified update path that preserves saves, preferences and connection files, with a recoverable previous kit. Avoid silent partial updates while a game is running.

**Done when:** a new player can join from the ZIP without network commands, recover from a dropped connection, and understand a pending change without opening a log. Test from a clean Windows account and two different Internet connections.

## 6. Consider three or four players

Start this only after two-player Internet play passes the release checks below. Define capture groups, shared deaths, type rules, votes and absent-player behavior first. Then assess protocol and save-format migration, UI space and relay fan-out.

**Done when:** three or four separate PCs pass the same correctness and responsiveness checks. Compare relay traffic and local frame pacing with the two-player baseline. Do not promise the same performance before measuring it.

## Release checks

Every network milestone needs automated native ROM/Lua tests and a session between two real PCs on different Internet connections through the VPS. Include ordinary home connections and a Wi-Fi/mobile connection where available.

Exercise variable delay, bandwidth limits, connection loss and restarts. Check captures before the partner joins, starters, duplicate encounters, shared deaths, all defeat-dialogue choices, team approval, full-capacity recovery and quick loads. No lost pair, duplicate event, revived Pokémon or corrupted save is acceptable.

Keep transport wait, safe-application wait and browser refresh delay separate in reports. Test normal emulator speed as well as turbo. Keep the existing TLS checks, ordering, checksums and save protections unless a separately validated replacement is needed.

Next, measure complete game-frame pauses, death cleanup and team movement.
Forced native partial-commit recovery has passed for both player roles through
the public relay, with both emulator cores on this PC.
Capture recovery after unsaved game termination needs a
defined ownership policy before a journal can restore it. Separate-PC Internet
acceptance remains required. More servers, a new transport and additional players
should wait for evidence that they solve a measured problem.
