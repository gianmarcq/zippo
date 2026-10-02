#!/usr/bin/env bash
# bench.sh - benchmark harness for zippo.

set -uo pipefail

ZIPPO=${1:-./zippo}
CORPUS=${2:-./canterbury}
THREADS=${THREADS:-"1 2 4 8"}
ITER=${ITER:-3}
SIZE_MB=${SIZE_MB:-64}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

now_ns() { date +%s%N; }

# fmt100 <n> - print n/100 with two decimals, e.g. 5967 -> 59.67
fmt100() { printf '%d.%02d' $(($1 / 100)) $(($1 % 100)); }

# best_ms <cmd...> - run ITER times, print the smallest wall time in ms
best_ms() {
    local best= ms t0 t1
    for ((i = 0; i < ITER; i++)); do
        t0=$(now_ns)
        if ! "$@" >/dev/null; then
            printf '%s failed: %s\n' "$ZIPPO" "$*" >&2
            return 1
        fi
        t1=$(now_ns)
        ms=$(( (t1 - t0) / 1000000 ))
        if [ -z "$best" ] || [ "$ms" -lt "$best" ]; then
            best=$ms
        fi
    done
    printf '%d\n' "$best"
}

# --- build the input ---------------------------------------------------------

if ! ls "$CORPUS"/* >/dev/null 2>&1; then
    printf 'no files found in %s\n' "$CORPUS" >&2
    exit 1
fi

IN="$TMP/in"
: > "$IN"
target=$(( SIZE_MB * 1024 * 1024 ))
while [ "$(stat -c%s "$IN")" -lt "$target" ]; do
    for f in "$CORPUS"/*; do
        [ -f "$f" ] || continue
        cat "$f" >> "$IN"
    done
done
insize=$(stat -c%s "$IN")

printf 'zippo benchmark\n'
printf '  binary : %s\n' "$ZIPPO"
printf '  corpus : %s\n' "$CORPUS"
printf '  input  : %s bytes (%s MiB)\n' "$insize" "$(( insize / 1048576 ))"
printf '  iters  : %s (best-of)\n\n' "$ITER"

printf '%-4s %9s %9s %8s %6s %6s  %s\n' ths comp decomp 'ratio%' 'c-x' 'd-x' check

# --- measure each thread count ----------------------------------------------

OUT="$TMP/out.zp"
DEC="$TMP/dec"
base_c=
base_d=

for TH in $THREADS; do
    if ! c=$(best_ms "$ZIPPO" -c -j "$TH" "$IN" "$OUT"); then
        printf '%-4s  compression failed\n' "$TH" >&2
        continue
    fi
    csize=$(stat -c%s "$OUT")

    if ! d=$(best_ms "$ZIPPO" -d -j "$TH" "$OUT" "$DEC"); then
        printf '%-4s  decompression failed\n' "$TH" >&2
        continue
    fi

    if cmp -s "$IN" "$DEC"; then check=OK; else check=FAIL; fi

    ratio=$(fmt100 $(( csize * 10000 / insize )))
    cx=$(fmt100 $(( ${base_c:-$c} * 100 / c )))
    dx=$(fmt100 $(( ${base_d:-$d} * 100 / d )))

    # first row is the baseline for every speedup
    [ -n "$base_c" ] || base_c=$c
    [ -n "$base_d" ] || base_d=$d

    printf '%-4s %9s %9s %8s %6s %6s  %s\n' "$TH" "$c" "$d" "$ratio" "$cx" "$dx" "$check"
done
