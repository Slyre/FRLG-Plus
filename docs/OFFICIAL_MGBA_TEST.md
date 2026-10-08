# Official mGBA test kit

`builds/lgfr-sl-official-test.zip` is separate from the regular `lgfr-sl.zip`.
It contains the unchanged official mGBA 0.10.5 Windows 64-bit portable files,
the same ROM and network companion, and a launcher adapted to manual scripting.
Portable Python is downloaded on the first launch. No Windows security settings
are changed. The official executable is unsigned, so acceptance by Smart App
Control must be tested on the affected PC.

After creating or joining a room with `play.bat`, use **Tools > Scripting >
File > Load script** in mGBA and choose `soul-link.lua` in the test folder.
The launcher selects the script for player A or B automatically and waits for
fresh game telemetry before confirming that the script has connected. It opens
the browser companion automatically. Reload the script after restarting mGBA
or resetting the scripting engine.

Extract the test kit into a separate folder and close the regular kit before
launching it on the same PC. Keep the original saves and connection/death
journals. Do not mix records from different runs.

To rebuild, obtain and extract the official portable archive linked from
[mGBA downloads](https://mgba.io/downloads.html), then use a fresh target:

```powershell
python -X utf8 remote/package_official_test.py pokefirered.gba build/soul-link-symbols.txt build/distribution/lgfr-sl-official-test OFFICIAL_FOLDER OFFICIAL_ARCHIVE
```

The manifest records the official archive hash and every bundled file hash.
The builder never patches the official emulator binaries and never replaces
the regular player ZIP.
