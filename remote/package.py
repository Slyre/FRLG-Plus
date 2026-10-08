"""Generate scripts bound to the current ROM; build the native test fixture."""
import argparse
import hashlib
from pathlib import Path
import shutil
import struct
import zipfile
import zlib


def generate_game(rom, symbols, target):
    rows = {}
    for line in symbols.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) == 3:
            rows[parts[2]] = int(parts[0], 16)
    target.mkdir(parents=True, exist_ok=True)
    data = rom.read_bytes()
    crc = struct.pack(">I", zlib.crc32(data))
    template = Path(__file__).with_name("bridge.lua.in").read_text(encoding="utf-8")
    for role in range(2):
        script = template
        for marker, value in {
            "ENABLE": hex(rows["gSoulLinkRemoteEnabled"]),
            "MAILBOX": hex(rows["gSoulLinkRemoteMailbox"]),
            "CONTROL": hex(rows["gSoulLinkCompanionControl"]),
            "STATUS": hex(rows["sSessionStatus"]),
            "ROLE": str(role),
            "CRC": '"' + "".join("\\%03d" % b for b in crc) + '"',
        }.items():
            script = script.replace("@" + marker + "@", value)
        (target / ("player-" + "ab"[role] + ".lua")).write_text(script, encoding="utf-8", newline="\n")
    shutil.copyfile(rom, target / "lgfr-sl.gba")
    return data


def build(rom, symbols, target):
    data = generate_game(rom, symbols, target)
    shutil.copyfile(Path(__file__).with_name("network.py"), target / "network.py")
    shutil.copyfile(Path(__file__).with_name("companion_core.py"), target / "companion_core.py")
    (target / "SHA256.txt").write_text(hashlib.sha256(data).hexdigest() + "  lgfr-sl.gba\n")
    with zipfile.ZipFile(target.with_suffix(".zip"), "w", zipfile.ZIP_DEFLATED) as archive:
        for name in ("lgfr-sl.gba", "player-a.lua", "player-b.lua", "network.py", "companion_core.py", "SHA256.txt"):
            archive.write(target / name, target.name + "/" + name)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("symbols", type=Path)
    parser.add_argument("target", type=Path)
    args = parser.parse_args()
    build(args.rom, args.symbols, args.target)
