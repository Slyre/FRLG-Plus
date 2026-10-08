# Soul Link

See the [Internet play roadmap](ROADMAP_INTERNET.md) for planned improvements and release criteria.

The clean player kit is `C:/Pokemon Soul Link/builds/lgfr-sl.zip`. Send that ZIP, not a personal game folder. See the [player guide](../remote/README.txt) for setup and updates.

## Playing together

The companion's History page keeps the latest 128 observations locally in
`duo-history.json`, scoped to the room and role. Replayed readings, provisional
ID promotion and companion restarts do not duplicate capture records. Terminal
observations cannot be replaced by older living readings. Times are receipt
times, not claimed capture times. This optional display record never restores
game data, drives synchronization, or travels through the relay.

Your duo now supports planning a roster beside either party and from a compact
list of living pairs, including boxed pairs. Selection only prepares a proposal;
the existing partner vote and native team validation still apply. Pair details
explain failed encounters, deaths, missing captures, pending application and
type conflicts. The type preview uses exported species types from this ROM's
source and checks the two Pokémon within each pair, not different pairs.

Disconnected partners retain their last reading for display. Raw upload data
is discarded on disconnect, readiness is false, and stale display data does not
authorize proposals or ordinary application. A short browser polling failure
shows an automatic retry message before suggesting a launcher restart.

Each player runs `play.bat`. One creates a room and shares the copied `SL2:` code; the other joins with that code. The launcher opens mGBA, automatically loads the A/B script and opens the local web companion in the default browser. `companion.bat` reopens the page. Keep both launcher consoles open.

The room creator's code is also displayed in the console, even if clipboard
copying fails. `connect.bat` prepares the network and opens the companion without
launching mGBA. Open mGBA and the ROM manually, then load the prepared
`soul-link.lua` from **Tools > Scripting > File > Load script**.

The network watches a handle to its Windows launcher process and stops when
that process exits, including abrupt console closure. It then closes its local
bridge and removes the web registration. Verified abandoned sessions from
another Soul Link kit using port 8766 can be cleaned up; active games and live
launchers are protected.

The relay runs at `gbfbc.com:8443` on Ubuntu VPS `193.70.89.162`. Players need Internet, without Tailscale, accounts or router changes. Saves stay on their PCs. Relay connections use verified TLS and ISRG X1/X2 roots from `relay-ca.pem`. There is no plaintext fallback.

## Local web companion

The Lua bridge reads a bounded local snapshot every 15 frames (about 250 ms
at normal speed) and waits for a fully initialized view. The coordinator wakes
on new telemetry or relay messages instead of waiting for its housekeeping
timer. A one-second retry guard replaces the old ten-second cooldown; final
views have up to two seconds to converge before retrying an identical exchange.
Unchanged WAN views retain their ten-second heartbeat, and desktop state writes
remain capped at twice per second. TCP_NODELAY and the 32-packet window stay in
use; the VPS protocol, acknowledgement ordering and save protections are unchanged.
The client checks pending joins every second and cancels an unstarted exchange
after ten seconds. This deadline does not apply to an already running transfer;
the existing progress and emulator deadlines still protect those transfers.

`remote/companion_web.py` runs in the network process, listening on `127.0.0.1` at an automatically chosen port. A random session path, Host/Origin checks and restricted routes protect local access. `compagnon-web.json` contains the current local URL and is removed on clean shutdown. Static files are in `remote/companion-web/`; sprites and English species names are in `remote/companion-assets/`. No web framework, remote asset service or extra runtime is required.

The page reads status once a second, or every three seconds while hidden. This polling does not trigger game synchronization. Actions go through the existing atomic local command queue. Both the web layer and the coordinator validate selections and votes. The ROM still validates changes before applying them. Closing a tab does not affect the game or network.

Both parties are shown with the last known safe HP and levels. During battle, the page keeps the last known party. A cold start can show a validated team with unknown HP. Run and participant IDs isolate cached parties. A safe empty reading clears the cache. Dead pairs cannot be selected. Either player can propose one to six living pairs; the other accepts or declines. Accepted teams stay queued until the game acknowledges them.

## Game behavior

Routine captures and evolutions use background views and local application. Players can battle independently; each receiving game applies a view only when it is available. Accepted team changes still wait until both games are outside battles, menus and dialogue and may briefly pause movement during their exchange. Death reports are separate and wait only for the receiving game to be free. Restarting or disabling standalone Nuzlocke does not revive the pair. The last dead Pokémon may remain in the party at zero HP; healing cannot make it usable.

The ROM menu keeps Rules, Your duo, Diagnostics and Back. Duo and pair pages are read-only. B cancels unapplied rule edits. Both games need matching rules. Manual room IDs, team selection and connection commands have been removed from the menu.

The Soul Link v3 save layout is unchanged. Keep `.sav`, connection files and settings when updating. Do not load an old emulator snapshot onto a new ROM. A fresh run needs two fresh saves and a new room code. A linked save keeps its original duo.

A starter received before connecting can be recovered from the party if it belongs to the player and was received at level 5 in Pallet Town. Multiple candidates prevent automatic recovery; existing or dead pairs are never recreated.

Tracking starts as soon as a new save is ready and its launcher reaches the relay,
even if the partner has not joined yet. Starter recovery also runs on safe
snapshots, so it does not need another capture to trigger it. In a Nuzlocke save,
an untracked party acquisition from before activation can be recovered when its
area is marked as consumed and exactly one owned Pokémon from that area remains.
Recovery requires first-encounter and same-area rules; ambiguous areas, Safari
subdivisions and existing failed or dead encounters are left alone.

With Dupes clause enabled, a Pokémon from a previously caught evolution family
is blocked from capture and does not use the area's encounter when defeated or
escaped. Duplicate history checks local capture records and caught Pokédex
entries as well as Nuzlocke flags; merely seeing a Pokémon does not count.
Recovered starters and early captures also restore their duplicate-history bits.

## mGBA and transport

mGBA stable 0.10.5 Soul Link revision 2 supports automatic scripts and SDL2 controllers. This release initializes English once, keeping button bindings and other preferences. Later language choices are preserved. Controls are under Tools > Settings > Keyboard or Controllers. `play.bat -Controls` opens mGBA without a ROM or script. Sources and licenses are published with the emulator; [build instructions](../../toolchain/BUILD-MGBA.md) describe the custom build.

Lua reads a 2996-byte RAM view. Lightweight network checks run at most every ten seconds; changes or explicit requests queue local application of a partner view. Team changes retain the native exchange. The transport uses a 32-message window, ordered acknowledgements and a renewed attempt ID. The ROM does not decrypt party data during unsafe phases.

`compagnon-etat.json` and `compagnon-commandes/` hold local UI state and requests. `morts-signalees.json` and `morts-en-attente.json` keep death reports for reconnects. These personal files are never packaged. The relay holds rooms, proposals and snapshots in memory. See [relay maintenance](../remote/vps/README.md).

Death records are scoped to the room and local role without storing the room
code. Each direction retains one run and at most 88 unique pair IDs in a file
limited to 4 KiB. Terminal records have no age expiry; a new run replaces that
direction's record. The game still checks run and participants before applying
a death. Records survive compatible ROM updates; the live transport still
requires both players to use the same ROM.

Writes flush and sync a temporary file before atomic replacement. Interrupted
replacement leaves the previous committed record readable. Incomplete,
oversized, corrupt, mismatched-room or invalid records are ignored. The checksum
detects accidental corruption; it is not authentication or protection against
someone editing their local files. Old unscoped records are read only if their
run and participants match the current room's derived identity. A write failure
keeps the record in memory, displays a message and retries at most every five
seconds. Keep the launcher open until the folder is writable again. This does
not guarantee recovery after power loss or storage hardware failure.

Reconnection resets the resend timer and wakes coordination immediately. The
sender repeats retained deaths every five seconds; duplicate deliveries merge
unique IDs and cannot undo terminal deaths. Captures remain recovered from game
state rather than restored from this death journal.

`team-recovery.json` holds the latest approved composition for this room, role
and ROM build. It is personal data, excluded from player packages and browser
diagnostic exports. The file is capped at 4 KiB and contains a checked proposal,
participants, normalized rules and pending/confirmed/cancelled state. Writes
flush and sync a temporary file before replacement; the checksum detects
accidental corruption. A failed pending-record write prevents the native team
command and retries at most every five seconds.

The ROM's command acknowledgement means the native exchange started, not that
the party changed. Approval remains pending until both game readings show the
selected IDs, the party lock and those IDs in the actual parties. One changed
party is displayed as a partial change, never as a confirmed shared team.
Native retry attempts have a ten-second guard and still wait for both games.

After companion restart or relay reconnection, the same saved proposal is sent
through the relay's existing proposal/vote path before native commands resume.
A player automatically votes only when their own saved agreement matches the
proposal identity, run, IDs and digest, and the current participants, rules and
living pairs still match. If an agreement is missing, that player must approve
again. A changed run, rules or registry invalidates the pending agreement and
requires a new proposal. Old ROM-build records are ignored after a ROM update.
Confirmed receipts remain available to recover an older game save, but never
authorize a different proposal. No relay deployment or save-format migration
is required. This is eventual recovery, not simultaneous or atomic two-PC
commit; the native exchange can still leave a temporarily different party.

Native recovery qualification on 2026-10-07 passed through the public TLS
relay with two actual mGBA cores on this PC. Both A-first and B-first party
changes were interrupted before the other game committed. Both companion
instances and their TCP connections were then replaced. Fresh instances
reported the actual partial change, reused the saved approval and confirmed
only after both native parties matched. Neither test required another vote.
The B-first fixture acknowledges transport receipt while withholding the final
native readiness packet from A; the replacement companion removes this
test-only delay. This does not establish performance on two separate PCs.

To reproduce, run the following in the configured WSL toolchain, once with
`SOUL_LINK_PARTIAL_FIRST=0` and once with `SOUL_LINK_PARTIAL_FIRST=1`:

```bash
SOUL_LINK_TEST_PUBLIC_RELAY=gbfbc.com SOUL_LINK_TEST_DELAY=0 \
SOUL_LINK_PARTIAL_TEAM_TEST=1 SOUL_LINK_PARTIAL_FIRST=0 \
bash tests/soul_link/run_companion_rom.sh
```

Run these tests sequentially: they share local ports and dedicated fixture
directories under `build/tests/desktop-a` and `build/tests/desktop-b`.

## Files

- `src/`, `include/`, `data/`: ROM and Soul Link rules.
- `remote/`: web companion, Lua bridge, networking, launcher and packaging.
- `remote/vps/`: Ubuntu service and deployment.
- `tests/soul_link/`: game, network, local web and launcher tests.
- `build/`: generated artifacts and isolated test fixtures.
- `../toolchain/`: compiler and mGBA cores for native tests.
- `../pokefirered-reference/`: vanilla reference.

## Building

```powershell
wsl.exe bash '/mnt/c/Pokemon Soul Link/toolchain/build-current.sh'
wsl.exe bash '/mnt/c/Pokemon Soul Link/toolchain/package-player.sh'
```

The package generator requires a fresh output folder to avoid overwriting player data. With symbols from the current ROM:

```powershell
python -X utf8 remote/package_minimal.py pokefirered.gba build/soul-link-symbols.txt build/distribution/lgfr-sl --relay gbfbc.com
```

## Tests

```powershell
python -X utf8 -m unittest tests.soul_link.test_network tests.soul_link.test_public_relay tests.soul_link.test_companion tests.soul_link.test_companion_web
node --test tests/soul_link/test_companion_web.mjs
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/soul_link/test_setup.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/soul_link/test_public_setup.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/soul_link/test_companion_open.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/soul_link/test_minimal_setup.ps1 -Package PACKAGE_PATH
wsl.exe bash '/mnt/c/Pokemon Soul Link/FRLG-Plus/tests/soul_link/run_cable_rom.sh'
wsl.exe bash '/mnt/c/Pokemon Soul Link/FRLG-Plus/tests/soul_link/run_companion_rom.sh'
python -X utf8 tests/soul_link/test_live_companion.py
```

Launcher tests use isolated copies. Native tests run real ROMs in mGBA. VPS tests use simulated players; testing on two remote PCs still checks the real player experience. [Project history](SOUL_LINK_HISTORY.md) summarizes previous approaches.

## Internet timing diagnostics

The companion retains up to 128 samples per timing, in memory for the current
process. The web Help page exports these measurements without game snapshots,
room codes, participant IDs or local session URLs. Relay RTT uses the existing
ping/pong path and local monotonic time; no server deployment is needed.

Stages: local digest change to confirmed synchronization; change queue to game
command; command to ROM acknowledgement; native START to relay BEGIN; BEGIN to
native FINISH; and local/partner busy intervals. These can overlap and must not
be summed. Transfer time includes native processing, not just wire latency.
Digest changes can represent several events; this is not per-capture tracing.
Cancelled changes can remain pending until a successful recovery. Byte counts
cover application frames after joining, excluding TLS/IP overhead and login.

Browser exports add at most 128 fetch/render samples and state-file age readings.
Fetch/render uses performance.now; state age uses the same PC wall clock and
can be affected by clock adjustments. State age is not capture-to-display time.
Actual remote-PC qualification and event-level tracing remain roadmap work.

## Differential Internet synchronization

Protocol 7 can exchange changed registry rows against the last completed,
canonical snapshot. Both games advertise the baseline count and digest. The
fast path is used only when those match; a missing or different baseline sends
the complete current registry. Initial pairing still uses the complete path.
The coordinator's merged proposal also sends only rows that differ from its
current snapshot, reconstructed and digest-checked by the follower.

Cleanup flags remain local and do not mark a shared row as changed. Snapshot
validation, ownership checks, terminal merge rules, commit barriers and team
readiness remain in place. Invalid state or digest conflicts discard the cache
so a retry can take the complete path. The cache is an optional heap allocation;
allocation failure leaves complete synchronization available. Every heap reset
clears the cache pointers before their memory can be reused.

The save layout stays at version 3. The ROM fingerprint changes, so both players
must update the ROM and its generated Lua scripts together. The public relay
needs no protocol change. This protocol now handles accepted team changes and manual native exchanges.
Routine registry updates use the background application path described below.

## Background registry application

The existing VPS view path carries game readings independently of a native
exchange. The companion keeps the raw partner view privately in memory; it is
not included in web state or diagnostic exports. A local kind-11 frame stages
bounded 48-byte chunks into a separate heap-owned inbox. Lua publishes an inbox
ID only after all 2952 bytes arrive at the same control pointer. Command 5 must
match that ID before the ROM can read the inbox. Heap resets or quick loads
cannot turn an incomplete old inbox into a current request.

The ROM checks scope, role, participant IDs, configuration, source generation,
count and pair digest, then uses the existing ownership and terminal-state
merge and commit functions. It applies only while locally idle and never writes
remote party data. A allocates pair IDs. B rejects an older A view that omits a
newer local capture; that capture stays in B's registry until A publishes a
matching view. Failed applications do not loop on the same pair of digests.
Busy requests can retry when the local game is free.

Routine application does not create a native network session or lock field
controls for a WAN response. Local validation and commit still take CPU time;
no fixed frame-pause budget has been certified. Accepted teams retain native
readiness and commit barriers and can pause movement during their exchange.
The control allocation grows to 5952 bytes; its existing 2996-byte outgoing
snapshot and save version 3 are unchanged. ROM and Lua scripts must be updated
together by both players.

### Local application cost

`tests/soul_link/run_apply_timing_rom.sh` executes the compiled routine in mGBA
and reads the release build's wrapping scheduler clock. `globalCycles` is
debugger-only in this mGBA build and must not be used for timing. Convert cycle
deltas at 16,777,216 Hz; a GBA frame is 280,896 cycles. The harness disables IRQs,
uses matching ROM symbols, checks successful commits, terminal deaths and the
absence of movement/network locks, and records stack depth from its fixture SP.
It does not measure host stepping time. The maximum fixture stack depth is
900 bytes; actual game call-chain stack budgets remain to qualify.

Table validation now uses a 256-byte reusable row index for IDs, encounter
ordinals and OT/personality identity. Occupied slots compare the complete key;
collisions probe another slot, including 88 forced collisions in sanitizer
tests. Zero IDs remain legal only where the previous player rules allow them.
Every field, row shape, terminal-state and local-ownership check remains.
Follower preparation caches at most 88 local row pointers during that call to
avoid repeatedly locating rows across split save banks. The cache is on the
stack, does not survive the call and never changes save data or protocol layout.

Identical 88-pair single-capture fixtures measured A/B costs of
452.974/424.238 ms before and 75.248/98.745 ms after. The one-death registry
application measured 98.741 ms on B and 74.821 ms on A for confirmation. A
57-capture recovery at 87 pairs reached 112.819 ms. Small registries incur a
fixed index cost: maxima of 1.576 ms for one pair and 4.491 ms for six pairs,
compared with 0.621 and 4.087 ms previously. These sample maxima are not a
population p95, universal budget or visible-pause guarantee. Registry fixtures
contain metadata rather than 88 boxed bodies. Linked-death body cleanup,
accepted-team PC movement, IRQs, other game-loop work, transport, browser refresh
and real two-PC frame pacing are not included.

### Indexed reconciliation and immediate commit

Follower preparation now indexes its local rows by allocated pair ID and
encounter ordinal. Commit validation indexes nonzero IDs; pending local
candidates retain their exact ordinal search. Hash collisions always compare
the complete key. Temporary indexes hold at most 88 entries in 256 slots and
are discarded on return. Missing or conflicting identities, incompatible
areas, omitted local captures and terminal observations still follow the same
rules as the frozen prior implementation in `reference_merge.inc`.

`SoulLink_ApplyRemoteState` prepares and fully validates a fresh scratch buffer,
then commits immediately without handing that buffer back to an intervening
caller or yielding to task/network logic. Its private write helper is used only
after full validation. Preparation's direct table check is not repeated where
commit validation already performs that same check. The separate
`SoulLink_CommitRemoteState` API still revalidates staged rows for the native
protocol, where frames and other changes can intervene. Input/output aliasing
is refused by the immediate API. This does not change save version 3, RAM view
layout or wire protocol, and does not make two-PC commits simultaneous.

The same benchmark now measures A/B single-capture costs at 88 pairs of
40.547/42.187 ms, compared with 75.248/98.745 ms in the previous release. B's
one-death update takes 42.059 ms and A's confirmation 40.122 ms. The 87-pair
bulk catch-up maximum falls from 112.819 to 79.017 ms. Small-registry maxima
are 1.107 ms (one pair), 3.172 ms (six) and 18.402 ms (thirty). These figures
retain the isolated-cycle limitations above; they exclude body cleanup and
visible frame work. Peak stack depth below the fixture SP is 1680 bytes;
production call-chain stack budgets still need qualification.

Sanitizer tests compare 4000 merge cases to the frozen prior implementation
and 4000 immediate applications byte-for-byte to the revalidated staged path,
including rejected changes, pending candidates, shuffled rows, stale identities,
deaths and area mismatch. Forced 88-way collisions exercise full-key resolution.

### Frame-sliced background application

Command 5 now keeps a temporary reconciliation job and advances it once per
available game frame. Remote validation, ownership merging, candidate pairing,
terminal-death propagation and commit validation use bounded batches. The job
holds private copies of the incoming rows and the complete local registry.
Partial work never changes the save. The final registry write stays atomic.

Before every batch, the complete local header and every stored pair are compared
byte-for-byte with the starting snapshot. A capture, death, evolution, cleanup,
rule change, team selection or run reset invalidates the job. Result 3 tells the
companion to obtain fresh readings and retry. Entering a battle or menu also
cancels unfinished application. Changed command IDs discard the old job, and
world/title heap resets discard its pointer with the rest of that heap.

Exact-key validation indexes are temporary job data. They compare full keys
after collisions and are rebuilt for each table-validation phase. The job
does not hold player movement or wait for a WAN acknowledgement. Accepted team
changes continue to use their separate native readiness and recovery protocol.
Save version 3, control layout, Lua packets and relay messages remain unchanged.

The native sanitizer suite compares 4000 sliced applications byte-for-byte with
the revalidated staged path. It checks that unfinished steps never write any
save bytes and interrupts every tested batch boundary for both player roles.
The same 10000 table-validator cases also exercise incremental exact-key checks.
Compiled-ROM timings report the largest individual callback, total CPU work and
callback count separately: spreading work can improve pacing while adding time
before the final update appears. These isolated timings still exclude the rest
of the frame, physical death cleanup, PC movement and Internet transport.

The final 42-sample ROM benchmark peaks at 7.928 ms per callback. At 88 pairs,
the longest fixture spans 35 callbacks with 131.068 ms of total CPU work; the
57-capture catch-up spans 49 with 159.239 ms. Setup runs on its own frame,
followed by batches of 16 row operations. Candidate matching costs four budget
units because it scans two tables. At normal speed those longest sequences
need about 0.57 and 0.80 seconds after setup, with gameplay between steps.
These are local processing intervals rather than measured Internet latency.
The temporary heap job is 9808 bytes and is freed on completion or rejection;
measured fixture stack depth below the saved SP is at most 120 bytes.

### Actual PC bodies and linked-death cleanup

Team and grave searches check the unencrypted OT/personality identity before
reading encrypted species. Matching bodies still use the normal species,
checksum, egg, mail, ambiguity and ownership checks. This avoids decrypting
unrelated bodies in every selected member's search.

On October 7, 2026, compiled-ROM fixtures tested both roles with 88 valid pair
records, six actual party members, and 390 or 420 actual boxed Pokémon. All six
selected bodies were placed at the far end of the PC. The largest preparation
cost fell from 1474.525 to 377.408 ms; physical death cleanup fell from 247.551
to 65.914 ms. The final six-body commit remains about 16.197 ms. Preparation
leaves the entire party and PC unchanged. All displaced members remain in the
PC; cleanup moves a dead member to a grave with zero HP when space exists and
retains pending cleanup when the PC is full.

These are isolated emulated CPU measurements with interrupts disabled, not
whole-frame pauses or Internet latency. Dense-PC team preparation is still
synchronous and can cause a noticeable pause. Save version 3 and protocol 7
are unchanged. Reproduce with `SOUL_LINK_TIMING_BODIES=1` when running
`tests/soul_link/run_apply_timing_rom.sh`; the detailed comparison is generated
under `build/tests/body-operation-results.json`.

Team preparation now reserves the slots freed by its boxed selections before
searching for additional empty slots. Displaced party members go into those
freed slots in selection order. A mixed composition searches only for the
remaining deposit slots, excluding graves and already reserved sources.
Preparation remains read-only; the commit copies all sources before clearing
them and depositing the outgoing members. A full PC still refuses a change
when the freed slots cannot hold the outgoing party.

The same six-member, 390/420-body fixtures now peak at 186.330 ms for preparation,
down from 377.408 ms. Commit and death-cleanup costs remain unchanged. Both roles
also pass unchanged-party and narrow-selection cases, including insufficient
space with the checked needed/available-slot diagnostic. Detailed results are
in `build/tests/team-slot-results.json`. This improves full swaps; team
preparation remains synchronous and these figures do not cover whole frames or
network latency.

Team searches now read the unencrypted OT/personality fields directly and
obtain each box's contiguous body array once per selected member. Encrypted
species still uses the normal accessor for matching identities. The same
body fixtures, run on both previous and current ROMs, reduce the maximum
preparation cost from 186.330 to 139.954 ms. New refusal fixtures distinguish
a duplicate body from a matching OT alone or matching personality alone;
both builds pass. Existing full-PC, deposit preservation and read-only checks
also pass. Detailed isolated CPU results are in
`build/tests/team-lookup-results.json`. Preparation remains synchronous.

The shared local-identity search now resolves each contiguous save bank once
and advances within that bank. It never steps across a bank into unrelated
save fields. Search order, OT/personality matching, species exclusions and
missing-output behavior stay unchanged; no persistent identity cache is added.
Sanitizers check 46992 hit/miss queries over both roles and every registry size
from 0 to 88, comparing output indices/rows and all save bytes.

Identical previous/current ROM fixtures reduce team preparation from 139.954
to 114.882 ms and physical death cleanup from 65.914 to 52.716 ms. Commit peaks
at 16.126 ms. These remain isolated CPU costs with interrupts disabled; whole
frames and separate-PC Internet performance are unqualified. Results are in
`build/tests/bank-search-results.json`. Optional
`SOUL_LINK_TIMING_PROFILE=1` emits exclusive instruction-address attribution for
selected routines; unlisted callees are not included in those per-routine totals.

Selected-pair reads during team preparation now use a bounded per-call cache.
A cursor reads only as far as needed for the current selection and retains
other chosen pairs encountered along the way. Each registry row is read at
most once. Validation and refusal priority still follow selection order;
nothing is cached across frames or calls. Actual-ROM tests on both builds
check a duplicate first member before a later invalid ID, and invalid or
missing later IDs after a valid first member.

At 88 pairs, the same six-box swap falls from 114.881 to 106.093 ms. Unchanged
six-party preparation falls from 26.833 to 26.568 ms. Newly timed single-member
compositions need up to 208.128 ms with dense/full PCs because outgoing members
still require vacancy searches; the single-party fixture rises by 0.013 ms,
while the single-box fixture improves by 0.087 ms. This is not an across-the-board
latency reduction. Fixture stack depth peaks at 628 bytes below its saved SP,
versus 420 before. These are isolated IRQ-disabled routine measurements.
Full results are in `build/tests/team-cache-results.json`.

The web companion now previews the Pokémon in a team proposal and keeps the
approved composition visible across tabs until the change is confirmed. Each
game has its own progress line. A receipt requires an active current reading,
the approved run, the locked selection and matching actual party; stale or
disconnected readings do not count. These indicators do not authorize or apply
a team change. The native confirmation remains authoritative.

Further ROM micro-optimization is paused pending separate-PC Internet testing.
The remaining synchronous team operations are occasional and the measured
isolated costs do not establish whole-frame or WAN performance.

The collapsible Connection panel separates relay connectivity from each game’s
activity and availability. Combat is shown as connected, not as a network fault.
Stale browser readings clear the indicators. It uses published state only and
adds no relay requests. `relayConnected` is an additive local UI field.

Browser polling checks the timestamp before rendering a received snapshot.
Expired, missing, nonnumeric or excessively future timestamps never replace
the last valid party, proposal or draft selection. Actions and progress receipts
remain unavailable until a fresh reading arrives. The isolated browser fixture
verified a stale different-run empty party cannot clear the existing selection.

Returning to the visible companion triggers an immediate local poll. A single
scheduler coalesces wake requests during an in-flight fetch and replaces the
existing timer, preventing parallel polling loops. Hidden-tab polling remains
slower; native and Internet sync are unaffected. Species fetches now share the
four-second timeout so first-load failure cannot stall polling indefinitely.

Team drafts can be edited using the last valid cached readings during a local
companion interruption. Draft edits update all three pickers without rendering
an old connection status. Sending and votes still require fresh state. Requests
disable draft controls until completion. New living-pair reconciliation removes
dead or missing selections on reconnect. A selected pair that acquires a type
conflict can still be deselected. Browser QA verified offline checkbox/count
agreement, disabled sending, preserved failure status, and removal of a newly
dead pair on reconnect followed by restored sending.

Queued UI commands now carry a per-bridge session nonce, a creation timestamp
and the local run/participants observed at submission. The consumer rejects
commands older than 15 seconds, from a previous bridge session, or for a changed
duo before any network or game action. Legacy unscoped UI files are discarded
with a retry message. This does not expire approved-team or death journals.
HTTP timestamp validation also rejects nonfinite, nonnumeric and excessively
future values. Integration tests cover actual HTTP submission, bridge consumption
and refusal after bridge restart; the relay protocol and ROM are unchanged.

Relay disconnect now clears live partner flags, pending peer snapshot bytes,
transient proposal and sync attempt state while preserving the last display
reading and durable death/team records. The fresh-view barrier also applies
when a previously seen partner disconnects. An active pong does not clear it;
a decoded view received on the current connection does. Until then, native
coordination and team approval stay pending. Existing recorded linked deaths
can still apply locally. Actual loopback TCP disconnect/rejoin tests verify
retained display, disabled old activity and release on a new peer view. These
tests do not qualify two separate PCs or WAN delay.

Native regression fixtures now submit commands through CompanionWeb.queue
using the published session/run metadata. Automatic test votes wait for the
proposal to appear in the published view. The manual transport fixture runs
its quick-load/cancellation section at normal frame pacing even on loopback:
its existing socket deadlines are wall-clock based. No assertions were removed.
The previous published transport also failed the accelerated cancellation
fixture; both native fixtures pass after correcting these fixture assumptions.

The shared native menu fixture now waits for the actual start-menu input task
with a bounded frame deadline, retrying START only while the overworld is idle.
A single fixed press could be ignored during an in-flight background apply on
the public relay. The menu-task and eight-item assertions remain mandatory.

UI requests are now claimed by atomic rename to a session-specific processing
file before parsing and execution. Only pending .json files are scanned; an
unremovable consumed marker cannot replay after a loop iteration or restart.
Claim failure leaves the request pending without side effects. This is at-most-
once local consumption, not a delivery acknowledgement or durable approval:
a crash between claim and send requires the player to inspect state and retry.
Death and approved-team journals are separate and unchanged. Failure-injection
tests cover all four actions, failed claim, restart and HTTP disk failure with
a successful later request. Filesystem queue errors return actionable JSON.

The per-connection heartbeat task now aborts its own transport when it exits,
including network failure or cancellation. This unblocks the receive loop
immediately rather than leaving it waiting for the 20-second read deadline
or TLS graceful shutdown. Cleanup retrieves expected network errors and retains
the existing reconnect backoff. A real loopback test verifies recovery within
four seconds after injected heartbeat failure; an explicit public TLS probe
rejoined in 1096.7 ms while the other client stayed connected. This measures
one detected-failure recovery on this PC, not Internet ping or native gameplay.
The probe uses an isolated temporary room and certificate verification.

Timing reports include the monotonic age of the latest sample. Disconnect
invalidates the relay sample age while keeping the bounded historical timing
summary. The browser only displays a finite nonnegative relay sample with an
age of at most 15 seconds on a current connected reading. Old kits without
age metadata show no live latency value. Metrics tests and the disconnect
hook verify invalidation and preserved history; browser QA confirms a recent
123 ms reading appears and a 20-second-old reading disappears. No WAN traffic
is added. The guide distinguishes relay round trips from game-apply waiting.
