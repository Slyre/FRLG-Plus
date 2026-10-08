# Audit corrections — 8 October 2026

Corrections to the six findings in AUDIT_2026-10-08.md:

- The desktop coordinator contains expected I/O/transport timeouts instead of exiting. Pending death records remain durable and are retried; cancellation and unexpected programming errors still propagate.
- Recorded eggs no longer run the standalone encounter quota check again at hatch. Unrecorded eggs and standalone Nuzlocke retain the old checks.
- Nincada evolves normally into Ninjask in Soul Link, without generating an unpaired Shedinja bonus. Legacy bonuses sharing a registered Nincada/Ninjask identity remain stored but unusable and cannot be healed in Soul Link. They no longer count as a second parent identity in team preparation. Actual duplicate parents remain rejected; unrelated Shedinja are not classified as legacy clones.
- Optional history rejects non-object JSON before accessing its properties.
- Species changes in an existing history record come from that Pokémon owner's reading. Delayed partner copies cannot produce reverse evolutions; terminal death/failure reports still propagate.
- All canonical encounter locations and Safari subdivisions have searchable English labels.

The save format and protocol do not change. Both players must install the matching ROM/companion kit. Save in Pokémon and close games/launchers before updating; retain .sav files and settings. Load the normal save, not an old quick state. Previously recorded permanent deaths are not automatically reversed.

## Validation and delivery

- 95 Python tests passed, plus a focused rerun of the 3 new regression tests after strengthening the real coordinator/death replay check.
- 18 Node tests passed, including canonical area mapping and location search.
- The C ASan/UBSan suite passed.
- Native egg/evolution regression checks passed on the rebuilt ROM: legal hatch survives, standalone quota unchanged, no new unpaired bonus, legacy bonus cannot heal or kill its parent, Ninjask selection succeeds, genuine duplicates still fail, unrelated Shedinja unaffected.
- The current native two-core cable suite passed: menus, party changes and locks, deaths, save/reset/continue, cancellation, held mail and quick-load reconciliation.
- The obsolete standalone `toolchain/test-rom.sh` failed an old fixed-round protocol assertion on BOTH the pre-fix and fixed ROM. This was verified as a baseline fixture problem; the current cable suite and new focused native tests above passed.
- All 409 ZIP payload files match their manifest and ZIP CRC passes. The package contains no player saves, settings or connection secrets.

The active player emulator was left running. Installed kits were not overwritten. The delivery files are `C:/Pokemon Soul Link/builds/lgfr-sl.zip` and `C:/Pokemon Soul Link/builds/lgfr-sl.gba`; update both players after saving and closing games/launchers.

ROM SHA-256: `dfc242aa5139e372736174b6007aa5bcf658123399265b44ae7b7c7a9c0440ac`.
ZIP SHA-256: `0769aacfa2cc04fdd7f9ab9a2c89ea244aafbe6087ddc92bc285d1c3a5f0889d`.

No separate-PC Internet qualification or VPS deployment is claimed. The server protocol is unchanged; these fixes are delivered in the player kit.
