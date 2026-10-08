#!/bin/sh
# Прогон DSDUMP.EXE в DOSBox без окна: run.sh <рабочий_каталог> <секунд> [клавиши для autoexec]
# Результат: <рабочий_каталог>/DSDUMP.BIN (64 КБ на кадр).
# DEMOLEVEL=k — демо стартует с уровня k; KEEPMOV=1 — не затирать файлы,
# уже лежащие в каталоге (подготовленные LEVELn.MOV); HSKIP/HCOUNT —
# кадры, для которых пишется HEAP.BIN (по умолчанию 0/150); CYCLES — такты
# DOSBox (3000).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
WORK=$1; SECS=${2:-60}
mkdir -p "$WORK"
python3 "$HERE/mkdump_exe.py" "$WORK/DSDUMP.EXE" $DEMOLEVEL >/dev/null
for f in "$ROOT"/LEVEL?.BIN "$ROOT"/LEVEL?.MOV "$ROOT"/SPRITES.EGA "$ROOT"/SPRITES.CGA; do
    [ -n "$KEEPMOV" ] && [ -e "$WORK/$(basename "$f")" ] && continue
    cp -f "$f" "$WORK/"
done
chmod u+w "$WORK"/*
rm -f "$WORK/DSDUMP.BIN" "$WORK/HEAP.BIN" "$WORK/LOW.BIN"
cat > "$WORK/dosbox.conf" <<CONF
[sdl]
output = surface
[dosbox]
machine = ega
memsize = 16
[cpu]
core = normal
cputype = 386_slow
cpu_cycles = ${CYCLES:-3000}
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
