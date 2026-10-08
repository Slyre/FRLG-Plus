#!/usr/bin/env bash
set -euo pipefail
task_root='/mnt/c/Pokemon Soul Link'
export PATH="$task_root/toolchain/local/usr/bin:$PATH"
export LD_LIBRARY_PATH="$task_root/toolchain/local/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$task_root/FRLG-Plus"
arm-none-eabi-nm -n pokefirered.elf > build/tests/apply-timing-symbols.txt
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -Wno-unused-function \
    -include "$task_root/toolchain/mgba-lua-build/include/mgba/flags.h" \
    -I "$task_root/toolchain/mgba/include" -I "$task_root/toolchain/mgba-lua-build/include" \
    -I "$task_root/toolchain/local/usr/include" -I include \
    tests/soul_link/test_apply_timing_rom.c "$task_root/toolchain/mgba-lua-build/libmgba.a" \
    -L "$task_root/toolchain/local/usr/lib/x86_64-linux-gnu" -lpng -lz -llua5.4 -lm -lpthread -ldl \
    -o build/tests/soul_link_apply_timing_rom
build/tests/soul_link_apply_timing_rom pokefirered.gba build/tests/apply-timing-symbols.txt | tee build/tests/apply-timing.log
if [[ -n "${SOUL_LINK_TIMING_BASELINE_ROM:-}" ]]; then
    build/tests/soul_link_apply_timing_rom "$SOUL_LINK_TIMING_BASELINE_ROM" build/tests/apply-timing-symbols-before.txt | tee build/tests/apply-timing-before.log
fi
