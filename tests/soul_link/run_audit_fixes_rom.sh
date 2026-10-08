#!/usr/bin/env bash
set -euo pipefail
task_root='/mnt/c/Pokemon Soul Link'
export PATH="$task_root/toolchain/local/usr/bin:$PATH"
export LD_LIBRARY_PATH="$task_root/toolchain/local/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cd "$task_root/FRLG-Plus"
arm-none-eabi-nm -n pokefirered.elf > build/tests/audit_fixes_rom_symbols.txt
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -include "$task_root/toolchain/mgba-build/include/mgba/flags.h" -I "$task_root/toolchain/mgba/include" -I "$task_root/toolchain/mgba-build/include" -I "$task_root/toolchain/local/usr/include" -I include tests/soul_link/test_audit_fixes_rom.c "$task_root/toolchain/mgba-build/libmgba.a" -L "$task_root/toolchain/local/usr/lib/x86_64-linux-gnu" -lpng -lm -lpthread -ldl -o build/tests/audit_fixes_rom
build/tests/audit_fixes_rom pokefirered.gba build/tests/audit_fixes_rom_symbols.txt