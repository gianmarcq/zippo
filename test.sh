#!/usr/bin/env bash
# test.sh - test harness for zippo.

set -uo pipefail

ZIPPO=${1:-./zippo}
CORPUS=./canterbury

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT # clean up tmp directory before exiting

pass=0
fail=0

ok()  { printf '   [ OK ] %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '   [FAIL] %s\n' "$1"; fail=$((fail + 1)); }
run() { printf '[ RUN ] %s\n' "$1"; }

# roundtrip <input-file> <compress-threads> <decompress-threads> <label>
roundtrip() {
    local in=$1 cj=$2 dj=$3 label=$4
    if ! "$ZIPPO" -c -j "$cj" "$in" "$TMP/a.zp" >/dev/null 2>&1; then
        bad "$label: compress failed (-j $cj)"
        return
    fi
    if ! "$ZIPPO" -d -j "$dj" "$TMP/a.zp" "$TMP/a.out" >/dev/null 2>&1; then
        bad "$label: decompress failed (-j $dj)"
        return
    fi

    # x = decode(encode(x))
    if cmp -s "$in" "$TMP/a.out"; then
        ok "$label"
    else
        bad "$label: output differs from input"
    fi
}

IN="$TMP/in.bin"

run test_one_byte
printf 'Z' > "$IN"
roundtrip "$IN" 1 1 "1 byte"

run test_single_symbol
head -c 4096 /dev/zero | tr '\0' 'x' > "$IN"
roundtrip "$IN" 1 1 "single symbol -j1"
roundtrip "$IN" 4 4 "single symbol -j4"

# stress chunk splitting
run test_odd_sizes
head -c 4097 /dev/urandom > "$TMP/big.bin"
for n in 2 7 8 9 63 64 65 1023 4095 4097; do
    head -c "$n" "$TMP/big.bin" > "$IN"
    roundtrip "$IN" 4 4 "size $n bytes"
done

run test_asymmetric_threads
head -c $((1 * 2 ** 20)) /dev/urandom > "$IN"
for pair in "1 4" "4 1" "2 3" "3 2" "8 1"; do
    set -- $pair
    roundtrip "$IN" "$1" "$2" "1 MiB -c -j$1 -d -j$2"
done

run test_canterbury_corpus
if [ -d "$CORPUS" ]; then
    for f in "$CORPUS"/*; do
        [ -f "$f" ] || continue
        roundtrip "$f" 1 1 "$(basename "$f") -j1/-j1"
        roundtrip "$f" 4 1 "$(basename "$f") -j4/-j1"
        roundtrip "$f" 1 4 "$(basename "$f") -j1/-j4"
    done
else
    printf '  [SKIP] %s not found\n' "$CORPUS"
fi

# handle empty file
run test_empty_rejected
: > "$IN"
if "$ZIPPO" -c -j 1 "$IN" "$TMP/a.zp" >/dev/null 2>&1; then
    bad "empty input: should fail, exited 0"
else
    ok "empty input rejected"
fi

# handle corrupted file
run test_corrupt_rejected
printf 'data to compress' > "$IN"
if ! "$ZIPPO" -c -j 1 "$IN" "$TMP/a.zp" >/dev/null 2>&1; then
    bad "corrupt test: setup compress failed"
else
    # change the low byte of the magic number
    printf '\x20' | dd of="$TMP/a.zp" bs=1 count=1 conv=notrunc status=none
    if "$ZIPPO" -d -j 1 "$TMP/a.zp" "$TMP/a.out" >/dev/null 2>&1; then
        bad "corrupt magic: should fail, exited 0"
    else
        ok "corrupt magic number rejected"
    fi
fi

printf '========================================\n'
printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
