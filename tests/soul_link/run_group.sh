#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/tests
common=(-std=c99 -Wall -Wextra -Werror -DSOUL_LINK_FOUR_PLAYERS=1 -fsanitize=address,undefined -g -I tests/soul_link/include -I include)
gcc "${common[@]}" src/soul_link_group.c tests/soul_link/test_group_native.c -o build/tests/group_native
build/tests/group_native
gcc "${common[@]}" src/soul_link_group_flash.c tests/soul_link/test_group_flash.c -o build/tests/group_flash
build/tests/group_flash
