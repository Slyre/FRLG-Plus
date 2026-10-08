#!/usr/bin/env bash
set -euo pipefail
task_root='/mnt/c/Pokemon Soul Link'
export PATH="$task_root/toolchain/local/usr/bin:$PATH"
export LD_LIBRARY_PATH="$task_root/toolchain/local/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$task_root/FRLG-Plus"
mkdir -p build/tests
arm-none-eabi-nm -n pokefirered.elf > build/tests/cable-rom-symbols.txt
gcc -std=gnu11 -O2 -Wall -Wextra -Werror \
    -include "$task_root/toolchain/mgba-build/include/mgba/flags.h" \
    -I "$task_root/toolchain/mgba/include" -I "$task_root/toolchain/mgba-build/include" \
    -I "$task_root/toolchain/local/usr/include" -I include \
    tests/soul_link/test_cable_rom.c "$task_root/toolchain/mgba-build/libmgba.a" \
    -L "$task_root/toolchain/local/usr/lib/x86_64-linux-gnu" -lpng -lm -lpthread -ldl \
    -o build/tests/soul_link_cable_rom
build/tests/soul_link_cable_rom pokefirered.gba build/tests/cable-rom-symbols.txt
