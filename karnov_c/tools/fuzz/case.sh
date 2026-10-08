#!/bin/sh
# case.sh <каталог> <уровень> <seed> <секунд> — оригинал (DOSBox) и перевод
# на одном случайном демо с уровня k; сверка DS (tools/dsdiff.py).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
KC=$(cd "$HERE/../.." && pwd)
W=$1; L=$2; S=$3; T=${4:-120}
mkdir -p "$W"
for n in 0 1 2 3 4 5 6 7 8 9; do
    python3 "$HERE/genmov.py" "$W/LEVEL$n.MOV" $((S * 10 + n))
done
DEMOLEVEL=$L KEEPMOV=1 sh "$KC/tools/dsdump/run.sh" "$W" "$T" >/dev/null 2>&1 || true
N=$(( $(stat -c %s "$W/DSDUMP.BIN") / 65536 ))
(cd "$W" && timeout 600 "$KC/karnov" --headless --no-audio --fast --demo-level "$L" \
    --frames "$N" --dsdump "$W/port.bin" >/dev/null 2>&1) || true
python3 "$KC/tools/dsdiff.py" "$W/DSDUMP.BIN" "$W/port.bin" "$W/port.bin.mask" -k 12 | tee "$W/diff.txt"
# совпавшие случаи: дампы (сотни МБ) не нужны
if grep -q '^совпадает' "$W/diff.txt"; then
    rm -f "$W/DSDUMP.BIN" "$W/HEAP.BIN" "$W/LOW.BIN" "$W"/port.bin*
fi
