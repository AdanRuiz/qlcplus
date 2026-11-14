#!/bin/bash
#
# Builds the application and runs every Director check. The checks are CMake
# targets (see CMakeLists.txt next to this file), so the same script works
# on Windows (MSYS2) and on Linux. Exit code 0 = everything passed.
#
# Windows, from an MSYS2 MINGW64 shell with Qt's bin directory at the END of
# the PATH (its MinGW runtime must not shadow the compiler's):
#
#   SRC=/c/path/to/sources BUILD=/c/path/to/build qmlui/director/test/run-tests.sh
#
# Linux container, with the repository on /src and a build directory on /build:
#
#   docker build -t qlcbuild qmlui/director/test
#   docker run -d --name qlcb -v "$PWD:/src" -v qlcbuildvol:/build qlcbuild sleep infinity
#   docker exec qlcb /src/qmlui/director/test/run-tests.sh
#
# The first run configures and builds everything; later runs only rebuild
# what changed.

SRC=${SRC:-/src}
BUILD=${BUILD:-/build}
NOISE="QFontDatabase\|Qt no longer ships fonts\|Detected locale\|Qt depends\|If this causes\|for more information\|unknown fixture"
FAILED=0

case "$(uname -s)" in
    MINGW*|MSYS*)
        EXE=.exe
        WORK=${WORK:-$BUILD/director-tests}
        # Applied after the build (see below): the engine DLL is not next
        # to the test executables, and Qt's DLLs need the MinGW runtime that
        # ships with Qt, which must come first to run but last to compile.
        RUNPATH="$BUILD/engine/src:$(dirname "$(command -v windeployqt)")"
        # Without a console, Qt would send its messages to the debugger
        export QT_FORCE_STDERR_LOGGING=1
        ;;
    *)
        EXE=
        WORK=${WORK:-/tmp/director-tests}
        ;;
esac
BIN=$BUILD/qmlui/director/test
T=$SRC/qmlui/director/test

fail() { echo "FAILED: $1"; FAILED=1; }

mkdir -p "$WORK" && cd "$WORK" || exit 1

echo "== Application and test build"
if [ ! -f "$BUILD/build.ninja" ]; then
    cmake -S "$SRC" -B "$BUILD" -G Ninja -Dqmlui=ON -DCMAKE_BUILD_TYPE=Debug > "$WORK/configure.log" 2>&1 || fail "configure (see $WORK/configure.log)"
fi
JOBS_ARG=${JOBS:+-j$JOBS}
[ -z "$JOBS_ARG" ] && [ -z "$EXE" ] && JOBS_ARG=-j6
(cd "$BUILD" && ninja $JOBS_ARG qlcplus5 director_tests > "$WORK/build.log" 2>&1) || { fail "build (see $WORK/build.log)"; grep -n "error" "$WORK/build.log" | head -20; }

[ -n "$RUNPATH" ] && export PATH="$RUNPATH:$PATH"

# Results go to a file: on Windows nothing reaches a redirected stdout
echo "== Decision engine (no QLC+ dependencies)"
"$BIN/directorengine_test$EXE" -o "$WORK/engine.txt,txt" > /dev/null 2>&1 || fail "engine test"
grep "FAIL\|Loc:\|Totals" "$WORK/engine.txt"

echo "== Manager against a real Doc"
QT_QPA_PLATFORM=offscreen timeout 120 "$BIN/directormanager_test$EXE" -o "$WORK/manager.txt,txt" > /dev/null 2>&1 || fail "manager test"
grep "FAIL\|Loc:\|Totals\|signal" "$WORK/manager.txt"

echo "== End to end: real Stage Wizard + Director + panel, inside the application"
BAR=$SRC/resources/fixtures/SteelPro/SteelPro-1030-Bar-43Ch.qxf
(cd "$BUILD/qmlui" && DIRECTOR_E2E_BAR_DEF=$BAR QLCPLUS_DIRECTOR_LOG="$WORK/director.log" \
    QT_QPA_PLATFORM=offscreen timeout 60 "$BIN/director_e2e$EXE" > "$WORK/e2e.log" 2>&1) || fail "end to end run (see $WORK/e2e.log)"
# -a: the function names carry non-ASCII dashes
grep -a "^FAIL\|E2E\|  layer\|signal" "$WORK/e2e.log"

# The rules file of a run before would be read back over the rules of this one
rm -f "$WORK"/*.director.json "$WORK"/*.director.json.bak

echo "== End to end: a rig of motorised bars only, project saved"
(cd "$BUILD/qmlui" && DIRECTOR_E2E_BAR_DEF=$BAR DIRECTOR_E2E_RIG=bars DIRECTOR_E2E_SAVE="$WORK/bars.qxw" \
    QT_QPA_PLATFORM=offscreen timeout 60 "$BIN/director_e2e$EXE" > "$WORK/e2e-bars.log" 2>&1) || fail "bars only run (see $WORK/e2e-bars.log)"
grep -a "^FAIL\|E2E\|  layer\|  rule\|signal" "$WORK/e2e-bars.log"

echo "== End to end: two SteelPro 1030 bars, two SteelPro Novas and two LED pars, project saved"
NOVA=$SRC/resources/fixtures/SteelPro/SteelPro-Nova.qxf
PAR=$T/data/Generic-AH40-DH41-18x18W-RGBW-Par.qxf
(cd "$BUILD/qmlui" && DIRECTOR_E2E_BAR_DEF=$BAR DIRECTOR_E2E_NOVA_DEF=$NOVA DIRECTOR_E2E_PAR_DEF=$PAR DIRECTOR_E2E_RIG=bars DIRECTOR_E2E_SAVE="$WORK/steelpro-rig.qxw" \
    QT_QPA_PLATFORM=offscreen timeout 60 "$BIN/director_e2e$EXE" > "$WORK/e2e-steelpro.log" 2>&1) || fail "SteelPro rig run (see $WORK/e2e-steelpro.log)"
grep -a "^FAIL\|E2E\|  layer\|signal" "$WORK/e2e-steelpro.log"

echo "== Panel: QML warnings and screenshot"
cd "$WORK" || exit 1
mkdir -p panel && cp "$SRC"/qmlui/qml/*.qml "$SRC/qmlui/qml/qmldir" "$SRC/qmlui/qml/director/DirectorPanel.qml" panel/
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software timeout 60 "$BIN/directorpanel_preview$EXE" panel/DirectorPanel.qml "$WORK/panel.png" 2>&1 | grep -v "$NOISE"
[ "${PIPESTATUS[0]}" = "0" ] || fail "panel preview"

echo
if [ "$FAILED" = "0" ]; then echo "ALL DIRECTOR CHECKS PASSED"; else echo "SOME DIRECTOR CHECKS FAILED"; fi
exit $FAILED
