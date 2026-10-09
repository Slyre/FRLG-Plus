# Four-player development

The two-player release is preserved at Git tag
`soullink-2p-stable-2026-10-08` (commit `12ba8ae51`). Development happens
on `soul-link-4-players`. The stable player ZIP and ROM must not be replaced
by an experimental package.

## Rules agreed with the players

- A linked group contains one Pokémon from each of four trainers.
- With linked deaths enabled, one death condemns the other three Pokémon.
- Encounters wait for the matching encounter from the other trainers.
- Changing the active groups requires all four trainers' approval.
- The type clause compares the Pokémon within each linked group, two at a time.
- Trainers can fight independently. Each game applies incoming changes when
  its own game is available; another trainer's battle does not freeze gameplay.
- Show each character's in-game trainer name. Seats remain internal stable
  identities; equal names get a seat number, for example `ASH (1)` and `ASH (3)`.

## Work plan

1. Preserve and push the tested two-player release. **Done.**
2. Implement and test the four-owner group model, terminal encounter handling,
   unanimous votes, and a compact native save codec.
3. Integrate the native runtime and save storage with explicit compatibility
   handling. Never reinterpret a two-player save as four-player data.
4. Add the versioned group bridge and client, then route four-player rooms on
   the relay without changing the two-player protocol.
5. Connect the four-trainer web interface, names, and launcher to that runtime.
6. Run four isolated emulator instances, delayed delivery, disconnections,
   reconnects, partial team application, save reloads, and duplicate-name tests.
7. Build a separate experimental player kit. Deploy compatible relay support
   only after integration tests pass. A four-PC Internet test remains a release
   acceptance check, rather than an assumed result of local tests.

## Constraints

The current native row is 32 bytes and contains two Pokémon. An unpacked
four-player row is 60 bytes. The existing reserve fields and the 128 KiB flash
layout cannot simply double in size. The planned codec stores each row in
54 bytes and derives its group state from the four encounter states. Existing
Hall of Fame, PC boxes, and Trainer Tower storage must be preserved.

EWRAM is also nearly full. The native runtime, companion buffers, and transient
save staging must be budgeted together before changing the heap allocation.
An isolated codec or relay passing tests does not make the complete game ready.

Four-player rooms use a separate protocol version and admission path. A slow
client must not hold the room lock while its socket drains. Votes and terminal
events remain ordered; replaceable owner readings may be coalesced.

Use a new four-player run initially. Any later migration from two players must
be explicit, use a new run identity, preserve terminal encounters, and clear old
team approvals. The original two-player save must remain recoverable.

## Progress — 2026-10-09

Implemented and tested in isolation:

- Four-owner native model and 4,868-byte codec; 38,944 single-bit corruption
  checks, all 24 owner arrival orders, four death directions, and explicit
  two-to-four migration validation.
- Scatter mapping into the existing Soul Link reserve bytes, unused data tails,
  and unused footer space. Available capacity is 5,212 bytes, leaving 344 bytes.
  Mapping tests preserve every game byte outside those spans. Production flash
  writes still need extension checksums, slot validation and recovery integration.
- Version-2 room domain, four votes and four separate application receipts,
  terminal event recovery records, owner-only telemetry and six type comparisons.
- Four TCP clients, sequence validation, disconnected owner retention,
  automatic seat assignment tied to a local installation credential, and bounded
  outgoing queues. Expired rooms and server restarts still need integration with
  client replay before being release-ready.
- Optional four-player routing in the public TLS server, disabled by default.
  The two-player and four-player namespaces are separate even with the same code.
- Four-team local web UI, retained in-game trainer names, duplicate-name suffixes,
  HTTP capability checks, and dark/light themes.

Not integrated yet: ROM runtime hooks, the production save path, the group Lua
bridge, network client and launcher, end-to-end four-emulator runs, memory/latency
measurements, and a playable four-player ZIP. No VPS change has been deployed.
The production player count remains two; the new C modules are compiled out
unless `SOUL_LINK_FOUR_PLAYERS=1` is explicitly defined.

Validation of this foundation: 117 combined Python tests passed; the additional
native/network contract regression subsequently passed with its focused suite.
All 32 JavaScript tests passed, as did native model/codec and flash mapping tests
under address/undefined-behavior sanitizers. The regular two-player ROM compiled
successfully. This does not measure a four-player ROM's frame time or heap usage.
