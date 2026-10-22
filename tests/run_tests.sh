#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# LUOGU's tests: the data compiled again (every bar the length of its meter, every syllable known; the header the
# same as the one built), the engine offline (a pattern played and cut, a 板式 sung through: the lead-in, the 大过门,
# the singing, a 小过门, the ending, in that order, then silence), the audio load, then the app in the host simulator
# (app/tests/basic.txt, ui.txt, logo.txt, kara.txt; tq.txt with 唱段). Exit 1 on any failure.
set -e
cd "$(dirname "$0")/.."
mkdir -p app/build/tests
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
FLAGS="-O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DLG_HOST -Iapp/dsp -Ibase/include
       -Itests/host"
before=$(cksum < app/dsp/lg_data.h)
"$PY" tools/lg_data.py
[ "$(cksum < app/dsp/lg_data.h)" = "$before" ] || { echo "FAIL app/dsp/lg_data.h was out of date (now rebuilt)"; exit 1; }
# shellcheck disable=SC2086
$CC $FLAGS -o app/build/tests/lg_render tests/host/lg_render.c app/dsp/lg.c app/dsp/perc.c app/dsp/hu.c -lm
# shellcheck disable=SC2086
$CC $FLAGS -o app/build/tests/perc_strokes tests/host/perc_strokes.c app/dsp/perc.c -lm
app/build/tests/perc_strokes app/build/tests >/dev/null
# 急急风, cut at 6 s: it plays its 收 and stops
out=$(app/build/tests/lg_render app/build/tests/jijifeng.wav pat 56 14 6)
echo "$out" | grep -q "^ *[0-9.]* shou *急急风" || { echo "FAIL 急急风: no 收"; exit 1; }
echo "$out" | tail -1 | grep -q "state 0" || { echo "FAIL 急急风: still playing"; exit 1; }
echo "急急风: played, cut, stopped"
# 西皮原板: a 小过门 cued at 26 s, the ending asked for at 33 s
out=$(app/build/tests/lg_render app/build/tests/xipi.wav bs 0 46 26 33)
flow=$(echo "$out" | awk '$2 == "qi" || $2 == "guomen" {print $2 ":" $3} $2 == "zhong" && !z {print "sing"; z = 1}' |
       tr '\n' ' ')
[ "$flow" = "qi:夺头 guomen:大过门 sing guomen:小过门·落2 qi:收头 " ] || { echo "FAIL 西皮原板: $flow"; exit 1; }
echo "$out" | tail -1 | grep -q "state 0" || { echo "FAIL 西皮原板: still playing"; exit 1; }
echo "西皮原板: $flow"
# a 唱段 (when this build has them: tq_data.h, the owner's own): 苏三离了洪洞县 from its 锣鼓 through its 京胡 to its
# closing 锣鼓, then silence
if grep -q "^#define TQ_N [1-9]" app/dsp/tq_data.h 2>/dev/null; then
    out=$(app/build/tests/lg_render app/build/tests/tq.wav tq 0 60)
    flow=$(echo "$out" | awk '$2 == "qi" || $2 == "tq" {print $2 ":" $3}' | tr '\n' ' ')
    [ "$flow" = "qi:凤点头 tq:西皮流水 qi:住头 " ] || { echo "FAIL 唱段: $flow"; exit 1; }
    echo "$out" | tail -1 | grep -q "state 0" || { echo "FAIL 唱段: still playing"; exit 1; }
    echo "唱段: $flow"
fi
# the 彩蛋 (when this build has it: guo_snd.h, the owner's own): 郭德纲's 包 plays out whole (0.91 s) and stops
if [ -f app/dsp/guo_snd.h ]; then
    out=$(app/build/tests/lg_render app/build/tests/guo_bao.wav say 0 2)
    echo "$out" | grep "^say" | awk '{exit !($4 >= 0.88 && $4 <= 0.97 && $7 > 0.1)}' ||
        { echo "$out" | grep "^say"; echo "FAIL 彩蛋: 包"; exit 1; }
    echo "彩蛋: $(echo "$out" | grep "^say")"
fi
# the audio, block by block as Felucca tests it: every 128-frame block (2.9 ms) rendered in 60 % of its time at most,
# the other 40 % left to the main loop (an estimate: instructions x X0X's calibration; macOS only)
# shellcheck disable=SC2086
$CC $FLAGS -o app/build/tests/lg_perf tests/host/lg_perf.c app/dsp/lg.c app/dsp/perc.c app/dsp/hu.c -lm
if [ "$(uname -s)" = Darwin ]; then
    out=$(app/build/tests/lg_perf 128 60) || { echo "$out" | tail -3; echo "FAIL audio load"; exit 1; }
    echo "audio load: $(echo "$out" | tail -1)"
fi
PYTHON="$PY" ./build.sh --host >/dev/null
scen="basic ui logo kara"
grep -q "^#define TQ_N [1-9]" app/dsp/tq_data.h 2>/dev/null && scen="$scen tq"   # (the 唱段: only with them)
[ -f app/guo_pic.h ] && [ -f app/dsp/guo_snd.h ] && scen="$scen guo"            # (the 彩蛋: only with it)
for s in $scen; do                      # (a failed check exits 1: the scenario's FAIL lines, then stop)
    out=$(app/build/host/luogu_host app/tests/$s.txt app/build/host) ||
        { echo "$out" | grep -E '^(FAIL|line )' || true; echo "FAIL $s"; exit 1; }
    echo "$s: $(echo "$out" | grep -c '^  ok') checks; $(echo "$out" | tail -1)"
done
echo "all passed"
