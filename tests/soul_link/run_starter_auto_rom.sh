#!/usr/bin/env bash
set -euo pipefail
task_root='/mnt/c/Pokemon Soul Link'
export PATH="$task_root/toolchain/local/usr/bin:$PATH"
export LD_LIBRARY_PATH="$task_root/toolchain/local/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$task_root/FRLG-Plus"
mkdir -p build/tests
arm-none-eabi-nm -n pokefirered.elf > build/tests/network-rom-symbols.txt
python3 remote/package.py pokefirered.gba build/tests/network-rom-symbols.txt "$task_root/FRLG-Plus/build/tests/network-rom-package"
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
    -include "$task_root/toolchain/mgba-lua-build/include/mgba/flags.h" \
    -I "$task_root/toolchain/mgba/include" -I "$task_root/toolchain/mgba-lua-build/include" \
    -I "$task_root/toolchain/local/usr/include" -I include \
    tests/soul_link/test_starter_auto_rom.c "$task_root/toolchain/mgba-lua-build/libmgba.a" \
    -L "$task_root/toolchain/local/usr/lib/x86_64-linux-gnu" -lpng -lz -llua5.4 -lm -lpthread -ldl \
    -o build/tests/soul_link_starter_auto_rom
SOUL_LINK_COMPANION_TEST=1 SOUL_LINK_COMPANION_BINARY=build/tests/soul_link_starter_auto_rom python3 tests/soul_link/network_rom_driver.py
