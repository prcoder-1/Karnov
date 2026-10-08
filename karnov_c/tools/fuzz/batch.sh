#!/bin/sh
# batch.sh <каталог> <секунд> <сиды...> — все уровни × сиды, по 4 параллельно;
# итог в <каталог>/summary.txt
HERE=$(cd "$(dirname "$0")" && pwd)
B=$1; T=$2; shift 2
mkdir -p "$B"
for s in "$@"; do for l in 0 1 2 3 4 5 6 7 8 9; do echo "$l $s"; done; done |
    xargs -P 4 -n 2 sh -c 'sh "$0/case.sh" "$1/l$3s$4" $3 $4 $2 > "$1/l$3s$4.log" 2>&1' "$HERE" "$B" "$T"
for f in "$B"/*.log; do printf '%s: %s\n' "$(basename "$f" .log)" "$(grep -m1 '^кадр [0-9]\|^совпадает' "$f")"; done > "$B/summary.txt"
cat "$B/summary.txt"
