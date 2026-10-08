#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/tests
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I tests/soul_link/include -I include \
    tests/soul_link/test_table_index.c -o build/tests/soul_link_table_index
build/tests/soul_link_table_index
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I tests/soul_link/include -I include \
    src/soul_link.c tests/soul_link/test_model.c -o build/tests/soul_link_model
build/tests/soul_link_model
gcc -std=gnu99 -g -ffunction-sections -fdata-sections -fsanitize=address,undefined \
    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
    -I tests/soul_link/flash_include -I tests/soul_link/include -I include \
    src/soul_link.c tests/soul_link/test_flash.c -Wl,--gc-sections -o build/tests/soul_link_flash
build/tests/soul_link_flash

gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I tests/soul_link/local_include -I tests/soul_link/include -I include \
    src/soul_link.c src/soul_link_local.c src/nuzlocke.c tests/soul_link/test_local.c -o build/tests/soul_link_local
build/tests/soul_link_local

gcc -std=c99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I tests/soul_link/include -I include \
    src/soul_link.c src/soul_link_protocol.c tests/soul_link/test_protocol.c -o build/tests/soul_link_protocol
build/tests/soul_link_protocol

gcc -std=c99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I tests/soul_link/link_include -I tests/soul_link/include -I include \
    src/soul_link.c src/soul_link_protocol.c src/soul_link_link.c tests/soul_link/test_link.c -o build/tests/soul_link_link
build/tests/soul_link_link

gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I tests/soul_link/include -I include \
    src/soul_link.c src/soul_link_protocol.c tests/soul_link/test_captures.c -o build/tests/soul_link_captures
build/tests/soul_link_captures

gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I tests/soul_link/include -I include \
    src/soul_link.c src/soul_link_protocol.c tests/soul_link/test_deaths.c -o build/tests/soul_link_deaths
build/tests/soul_link_deaths

gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I tests/soul_link/include -I include \
    src/soul_link.c src/soul_link_protocol.c tests/soul_link/test_reconcile.c -o build/tests/soul_link_reconcile
build/tests/soul_link_reconcile

gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
    -I tests/soul_link/local_include -I tests/soul_link/include -I include \
    src/soul_link.c src/soul_link_rules.c tests/soul_link/test_rules.c -o build/tests/soul_link_rules
build/tests/soul_link_rules
