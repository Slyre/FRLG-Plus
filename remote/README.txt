POKÉMON SOUL LINK — PLAY TOGETHER

Getting started

1. Extract the ZIP to a local folder on 64-bit Windows 10 or 11.
2. Run play.bat. It downloads the required software on the first launch.
3. One player chooses Create a room. The room code is shown in the console
   and copied automatically to the clipboard.
   Send it to your partner in a private message.
4. The other chooses Join a room and pastes the entire code, including SL2:.
5. mGBA and the companion open. Load your save or start a new game.
6. For a new duo, leave battles, menus and dialogue until the companion says
   Your duo is up to date. There are no IDs to enter in Pokémon.

Your game starts tracking captures before your partner joins. Starters received
before connecting are picked up automatically. With Nuzlocke enabled, a unique
first capture already in your party can also be recovered from its used area;
failed encounters and dead pairs are kept as they are.

You can play from different networks. You only need Internet: no Tailscale
account or router changes. Keep the launcher console open while playing.
Closing the launcher console stops the network and releases its local port.

Alternative: connect.bat starts the connection and browser companion while
leaving mGBA for you to open manually. Open emulator-soullink/mGBA.exe, then
File > Load ROM and choose lgfr-sl.gba. In Tools > Scripting > File > Load
script, choose soul-link.lua from the player folder. The launcher prepares
the correct A/B script. Save and close the game before pressing Q to stop
the connection. This mode does not bypass Windows application control.

The companion

Open Connection below the status message to check the relay and both games.
A battle or menu can delay an update without dropping the connection. The
panel uses existing readings; it does not send extra network requests.
Relay latency is shown only for a recent measurement on a connected session.
It is the server round trip, not the time for a game to apply an update.
If the companion stops responding, your last valid teams and selection stay
visible while it retries. Old readings cannot replace them.
You can still plan a team from those recorded pairs. Nothing is sent while
the companion is unavailable. On reconnect, dead or missing pairs are removed
from the draft before you can send it.
Requests waiting too long, or left over from a previous launcher session, are
discarded. If you see a request-expired message, check your selection and retry.
Each request is removed from the pending queue before execution. A failed
cleanup cannot send it twice. If the companion crashes during a request, check
the actual team and proposal before retrying; pending clicks are not replayed.
Returning to this tab checks for a new reading immediately. While it is in
the background, the browser checks less often; game-to-game sync keeps running.

History keeps the latest 128 updates on this PC: captures, evolutions, deaths,
pairs and team proposals. Times show when this companion received the update,
not the exact moment it happened in the game. It is a local record, not a save.
Keep duo-history.json when moving your game folder if you want to keep it.

You can plan a team directly in Your duo: select a pair beside either party,
or use the list of living pairs below. Propose team sends your selection for
your partner to approve. Selecting a pair alone never changes the game.
Choose your team explains waiting captures, lost encounters and type conflicts.
Search by Pokémon name or location, filter by status, and sort the pair list.
Review your planned team shows who stays, joins or leaves before you propose.

New proposals and lost pairs show a short alert. A background tab also shows
an unread update count in its title. In How to play > Alerts, you can enable
sound or desktop notifications. Both start off; desktop alerts depend on your
browser and Windows permissions. Older losses are not announced on opening.

History > This session counts new capture, death, evolution, failed encounter
and team-change records received while the launcher is open. Previously
unrecorded captures can appear here when connecting. Deaths count individual
Pokémon, so one lost pair can count twice. Reloading the tab keeps this tally;
restarting the launcher or switching to another duo resets it.


If the connection drops, the last partner reading stays visible and is marked
as old. HP may be out of date. Proposals wait for both games to reconnect and
agree on their pairs. There is no need to close the game for a short interruption.
After reconnecting, the companion waits for a new partner reading before
resuming team changes. A connected launcher alone does not confirm that the
old team is still current.

The companion is a local web page. play.bat opens it in your default browser;
companion.bat reopens it. Closing the tab does not stop the game or the network.
No website account, extra browser extension or manual address is needed.
Use Dark theme / Light theme to switch. Your browser remembers your choice;
the companion follows your Windows theme until you choose one.

Your duo shows both parties. HP and levels come from the last reading outside
battle, so they may lag behind during a fight. If no reading is available,
the companion shows known Pokémon with unknown HP instead of guessing.

Choose your team lists your pairs. Select one to six living pairs and click
Propose team. Either player can propose; the other accepts or declines.
The proposal shows the Pokémon on each side. After approval, its preview stays
visible while Your game and Partner’s game finish the change. Team updated
means a current game reading matches the approved party and selection. An old
reading after a disconnect does not count.
After approval, the games move Pokémon into or out of the PC. Leave enough
PC space and remove held mail that blocks transfers.
The change is confirmed only after both games show the approved party. If the
connection drops halfway through, keep the same game folders and relaunch
play.bat. The companion resumes the saved agreement. If your partner's copy
is missing, they will need to accept it again. A dead pair or changed rules
can cancel the old agreement; choose a new team in that case.
A lone capture waits for its partner. Dead pairs stay visible but cannot
be selected. Filters help you find living, waiting or dead pairs.

Captures, evolutions and deaths are tracked automatically. Local readings run
four times a second; unchanged readings are not sent repeatedly over Internet.
Changed readings travel in the background. Each game applies them when it is
free. Sync requests another check; it does not stop either game.
The page reads local status about once a second; this does not trigger a sync.
Team changes still need both games to be ready. If that exchange cannot connect
within ten seconds, the companion cancels the attempt and can retry later.

A starter received before connecting can be recovered if it is still in your
party, belongs to you and was received at level 5 in Pallet Town. Multiple
candidates prevent automatic recovery. A recorded or dead starter pair is
never recreated.

With Dupes clause on, you can skip a Pokémon from a family you have already
caught: run away or knock it out, then look for another encounter in that area.
Duplicates cannot be caught and do not use your attempt. The first non-duplicate
does count, so running away from it or knocking it out loses that area's catch.
Your partner's captures do not count as your duplicates.

Play at your own pace

You can battle and travel separately. Routine updates do not hold movement
while waiting for the Internet or your partner. Each game applies a received
reading outside battle, menus and dialogue. An accepted team change still waits
for both games and may briefly pause movement during its validated exchange.

Death reports are separate. They can be sent while one player is still on
the defeat dialogue, and apply when the receiving game is free, never during
its battle. Reports are kept on the PC and resent after reconnecting.
Keep the same game folder when updating: it contains your save and pending
death reports. If the companion says it cannot save reports, keep the launcher
open and check that the folder is writable. It will retry automatically.
Restarting or disabling standalone Nuzlocke does not revive a dead pair.
The last dead Pokémon may remain in the party at zero HP; others go to the
grave box when a transfer is possible.

To start over, use two fresh saves and a new room code. An already linked save
keeps its original duo. Use play.bat -ConfigureAgain to change the connection.
The in-game Soul Link menu keeps Rules, Your duo and Diagnostics. Both players
must choose matching rules. Teams are proposed in the companion.

Keyboard and controller

mGBA starts in English for this release.
Keyboard: Tools > Settings > Keyboard.
Controller: connect it, then Tools > Settings > Controllers.
Select the controller, assign its buttons and click OK.

Updating

Both players need the same ZIP. Save in Pokémon and close mGBA before replacing
files. Keep lgfr-sl.sav, your settings and connection files.
Load the in-game save; do not load an old emulator snapshot onto a newer ROM.
Old JOUER.bat, COMPAGNON.bat and COPIER-CODE.bat launchers are replaced by
play.bat and companion.bat. Copy the whole package, not just the ROM.

If something goes wrong

- Your partner cannot connect: check Internet and the room code. The creator
  needs to keep play.bat open.
- The team does not change: read the companion message, leave battles, menus
  and dialogue, then check held mail and PC space.
- The companion tab stopped: run play.bat again, then companion.bat.
- Windows application control blocked the emulator: check launch-diagnostic.txt
  and Windows Security > App & browser control. Send the diagnostic to the
  package maintainer. A new ZIP does not automatically remove a Windows policy
  block. Smart App Control may require trusted code signing for the emulator
  and its libraries; the launcher does not change Windows security settings.
- The game or network stopped: keep emulateur-erreurs.log and reseau-erreurs.log
  so the failure can be investigated.

Save in Pokémon before quitting. The companion does not save the game for you.
Saves stay on your PCs. The Internet relay uses verified TLS and keeps rooms
in memory; it does not store saves. The local web page listens only on your PC
and loads no external scripts, fonts or assets.
Send the clean ZIP to new players, not your personal game folder.

Network timings
---------------
The status line shows the latest round trip to the Internet relay, not the
round trip to your partner. Under Help, Download network timings exports
bounded measurements for this session: queued changes, game commands, transfer
time, busy games, reconnects and browser refreshes. The export contains no room
code or save data. Application byte totals exclude TLS/IP overhead and login.
These readings diagnose delays; they do not change sync or battle behavior.

Routine captures use validated background readings. Accepted team changes use
the confirmed-baseline protocol, with a complete exchange when that baseline
is missing or differs. Both players need this build's ROM and scripts. Keep your
.sav when updating; use a normal save instead of an older emulator snapshot.

Audit fixes — 8 October 2026

Recorded eggs use their encounter when received, not again when they hatch.
Standalone Nuzlocke keeps its existing hatch checks when Soul Link is off.
In Soul Link, Nincada evolves into Ninjask without the unpaired Shedinja bonus.
An old bonus sharing a registered Nincada/Ninjask identity stays stored but
cannot be used or healed; it no longer blocks choosing the Ninjask pair.
Previously recorded terminal deaths are not automatically undone.

Automatic network sends tolerate transport interruptions while the connection
worker reconnects. Invalid optional history is ignored at startup. Evolutions
are recorded from the Pokémon owner's reading, not a delayed partner copy.
All supported encounter locations now have names in the journal and search.

Both players should update together. Save in Pokémon, close both games and
launchers, then update the files while keeping your .sav and settings. Load the
normal save after updating; old quick states are not an update mechanism.
