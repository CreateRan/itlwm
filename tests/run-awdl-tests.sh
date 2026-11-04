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
