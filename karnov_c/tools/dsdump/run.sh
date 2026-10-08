#!/bin/sh
# Прогон DSDUMP.EXE в DOSBox без окна: run.sh <рабочий_каталог> <секунд> [клавиши для autoexec]
# Результат: <рабочий_каталог>/DSDUMP.BIN (64 КБ на кадр).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
WORK=$1; SECS=${2:-60}
mkdir -p "$WORK"
python3 "$HERE/mkdump_exe.py" "$WORK/DSDUMP.EXE" >/dev/null
for f in "$ROOT"/LEVEL?.BIN "$ROOT"/LEVEL?.MOV "$ROOT"/SPRITES.EGA "$ROOT"/SPRITES.CGA; do
    cp -f "$f" "$WORK/"
done
chmod u+w "$WORK"/*
rm -f "$WORK/DSDUMP.BIN" "$WORK/HEAP.BIN"
cat > "$WORK/dosbox.conf" <<CONF
[sdl]
output = surface
[dosbox]
machine = ega
memsize = 16
[cpu]
core = normal
cputype = 386_slow
cpu_cycles = 3000
[mixer]
nosound = true
[autoexec]
mount c "$WORK"
c:
DSDUMP.EXE
CONF
cd "$WORK"
SDL_AUDIODRIVER=dummy timeout "$SECS" xvfb-run -a dosbox -noprimaryconf -nolocalconf -conf "$WORK/dosbox.conf" >"$WORK/dosbox.log" 2>&1 || true
ls -l "$WORK/DSDUMP.BIN"
