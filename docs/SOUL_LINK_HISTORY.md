# Soul Link history

Use the [current guide](../remote/README.txt) to play and [project documentation](SOUL_LINK.md) to build. This file records previous approaches.

## ROM foundation

The project began with FRLG+, using `pret/pokefirered` as the vanilla reference. Persistent pairs identify Pokémon by personality and trainer ID. Tests cover save compatibility, interrupted writes, encounters, healing, PC transfers and registry limits.

The cable protocol first established capture pairing, linked deaths and reconciliation in two real mGBA cores. A known death survives a peer loading an older state. This alone cannot prevent two players restoring states from before a death that was never shared.

Type rules compare the two Pokémon within each pair. Automatic party changes use the PC and preserve the current team if held mail, missing Pokémon or storage capacity block a transfer.

## Remote play

The Lua bridge and Python companion carried native packets over Internet. Tailscale was initially used, then replaced with a verified TLS relay because its setup was too difficult for new players. The Windows launcher downloads pinned dependencies and joins with one room code.

mGBA stable was adapted to load the A/B script automatically. Revision 2 restored SDL2 controller support and included complete French Qt/mGBA catalogs. The current player release defaults to English; the binary remains the same.

## Companion and fixes

The first companion was a PowerShell/WPF window. Either player could propose a team and the other approve it. Subsequent fixes covered a missing packaged module, accepted teams being forgotten, starter recovery, deaths during defeat dialogue and abandoned network processes.

Death reports are persisted locally for reconnects. Party display keeps safe readings during battles and never invents unknown HP. The ROM menu was reduced to Rules, Your duo and Diagnostics; configuration and team proposals moved to the companion.

## Local web interface

The WPF window was replaced by a local browser page with a paper journal design, English text and English Pokémon names. It reuses the coordinator, native rules and command queue. Team selection, filters, partner approval, copy-code and sync are available without entering a URL. Closing the page leaves the game running.
