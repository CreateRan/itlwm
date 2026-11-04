#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/itlwm-awdl-tests.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
xcrun clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined     -fno-omit-frame-pointer -g -I. tests/awdl_frame_test.c -o "$test_dir/awdl-tests"
"$test_dir/awdl-tests"
xcrun clang++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/awdl_interface_test.cpp -o "$test_dir/awdl-interface-tests"
"$test_dir/awdl-interface-tests"
xcrun clang++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/awdl_lifecycle_test.cpp -o "$test_dir/awdl-lifecycle-tests"
"$test_dir/awdl-lifecycle-tests"
xcrun clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/awdl_data_test.c -o "$test_dir/awdl-data-tests"
"$test_dir/awdl-data-tests"
xcrun clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/awdl_sync_test.c -o "$test_dir/awdl-sync-tests"
"$test_dir/awdl-sync-tests"
xcrun clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/awdl_peers_test.c -o "$test_dir/awdl-peers-tests"
"$test_dir/awdl-peers-tests"
# Match the kernel target's diagnostics for idiomatic C aggregate zeroing.
xcrun clang++ -x c++ -std=c++11 -Wall -Wextra -Werror \
    -Wno-missing-braces -Wno-missing-field-initializers -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/awdl_peers_test.c -o "$test_dir/awdl-peers-cxx-tests"
"$test_dir/awdl-peers-cxx-tests"
xcrun clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/awdl_overlap_test.c -o "$test_dir/awdl-overlap-tests"
"$test_dir/awdl-overlap-tests"
xcrun clang++ -x c++ -std=c++11 -Wall -Wextra -Werror \
    -Wno-missing-braces -Wno-missing-field-initializers -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/awdl_overlap_test.c -o "$test_dir/awdl-overlap-cxx-tests"
"$test_dir/awdl-overlap-cxx-tests"
xcrun clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/iwx_txq_v2_test.c -o "$test_dir/iwx-txq-v2-tests"
"$test_dir/iwx-txq-v2-tests"
xcrun clang++ -x c++ -std=c++11 -Wall -Wextra -Werror \
    -Wno-missing-braces -Wno-missing-field-initializers -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/iwx_txq_v2_test.c -o "$test_dir/iwx-txq-v2-cxx-tests"
"$test_dir/iwx-txq-v2-cxx-tests"
xcrun clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/iwx_awdl_resources_test.c -o "$test_dir/iwx-awdl-resources-tests"
"$test_dir/iwx-awdl-resources-tests"
xcrun clang++ -x c++ -std=c++11 -Wall -Wextra -Werror \
    -Wno-missing-braces -Wno-missing-field-initializers -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/iwx_awdl_resources_test.c -o "$test_dir/iwx-awdl-resources-cxx-tests"
"$test_dir/iwx-awdl-resources-cxx-tests"
xcrun clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/iwx_flush_v1_test.c -o "$test_dir/iwx-flush-v1-tests"
"$test_dir/iwx-flush-v1-tests"
xcrun clang++ -x c++ -std=c++11 -Wall -Wextra -Werror \
    -Wno-missing-braces -Wno-missing-field-initializers -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/iwx_flush_v1_test.c -o "$test_dir/iwx-flush-v1-cxx-tests"
"$test_dir/iwx-flush-v1-cxx-tests"
python3 -B tests/iwx_firmware_audit_test.py
xcrun clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/iwx_station_adapter_test.c -o "$test_dir/iwx-station-adapter-tests"
"$test_dir/iwx-station-adapter-tests"
xcrun clang++ -x c++ -std=c++11 -Wall -Wextra -Werror \
    -Wno-missing-braces -Wno-missing-field-initializers -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/iwx_station_adapter_test.c -o "$test_dir/iwx-station-adapter-cxx-tests"
"$test_dir/iwx-station-adapter-cxx-tests"
xcrun clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/iwx_drain_adapter_test.c -o "$test_dir/iwx-drain-adapter-tests"
"$test_dir/iwx-drain-adapter-tests"
xcrun clang++ -x c++ -std=c++11 -Wall -Wextra -Werror \
    -Wno-missing-braces -Wno-missing-field-initializers -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g -I. tests/iwx_drain_adapter_test.c -o "$test_dir/iwx-drain-adapter-cxx-tests"
"$test_dir/iwx-drain-adapter-cxx-tests"
