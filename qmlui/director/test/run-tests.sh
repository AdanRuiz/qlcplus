#!/bin/bash
#
# Builds QLC+ 5 and runs every Director check. Meant for a Linux container
# with the repository mounted on /src and a build directory on /build:
#
#   docker build -t qlcbuild qmlui/director/test
#   docker run -d --name qlcb -v "$PWD:/src" -v qlcbuildvol:/build qlcbuild sleep infinity
#   docker exec qlcb /src/qmlui/director/test/run-tests.sh
#
# The first run configures and builds everything (about 20 minutes); later
# runs only rebuild what changed. Exit code 0 = everything passed.

SRC=${SRC:-/src}
BUILD=${BUILD:-/build}
WORK=${WORK:-/tmp/director-tests}
MOC=/usr/lib/qt6/libexec/moc
QTINC=$(pkg-config --variable=includedir Qt6Core)
NOISE="Detected locale\|Qt depends\|If this causes\|for more information\|unknown fixture"
FAILED=0

fail() { echo "FAILED: $1"; FAILED=1; }

mkdir -p "$WORK" && cd "$WORK" || exit 1

echo "== Application build"
if [ ! -f "$BUILD/build.ninja" ]; then
    cmake -S "$SRC" -B "$BUILD" -G Ninja -Dqmlui=ON -DCMAKE_BUILD_TYPE=Debug > "$WORK/configure.log" 2>&1 || fail "configure (see $WORK/configure.log)"
fi
(cd "$BUILD" && ninja -j"${JOBS:-6}" qlcplus5 > "$WORK/build.log" 2>&1) || { fail "application build"; grep -n "error" "$WORK/build.log" | head -20; }

INC="-I. -I$SRC/qmlui -I$SRC/engine/src -I$SRC/engine/audio/src -I$SRC/plugins/interfaces \
-I$SRC/plugins/midi/src/common -I$SRC/plugins/vertexav -I$SRC/qmlui/director -I$SRC/qmlui/fixtureeditor \
-I$SRC/qmlui/stagewizard -I$SRC/qmlui/tardis -I$SRC/qmlui/virtualconsole -I$SRC/hotplugmonitor/src \
-I$QTINC/Qt3DCore -I$QTINC/Qt3DRender -I$QTINC/Qt3DExtras -I$QTINC/Qt3DInput -I$QTINC/Qt3DLogic \
-I$QTINC/Qt3DQuick -I$QTINC/Qt3DQuickExtras"
PK="Qt6Core Qt6Gui Qt6Test Qt6Quick Qt6Qml Qt6Multimedia Qt6Widgets Qt6Network"
ENGINE_LIB="-L$BUILD/engine/src -lqlcplusengine5 -Wl,-rpath,$BUILD/engine/src"
T=$SRC/qmlui/director/test
D=$SRC/qmlui/director

echo "== Decision engine (no QLC+ dependencies)"
$MOC $T/directorengine_test.cpp -o directorengine_test.moc
g++ -std=c++17 -fPIC -I. $(pkg-config --cflags Qt6Core Qt6Test) $T/directorengine_test.cpp $D/directorengine.cpp \
    -o engine_test $(pkg-config --libs Qt6Core Qt6Test) || fail "engine test build"
./engine_test 2>&1 | grep "FAIL\|Loc:\|Totals"
[ "${PIPESTATUS[0]}" = "0" ] || fail "engine test"

echo "== Manager against a real Doc"
$MOC $D/directormanager.h -o moc_directormanager.cpp
$MOC $T/directormanager_test.cpp -o directormanager_test.moc
g++ -g -std=c++17 -fPIC -DQMLUI $INC $(pkg-config --cflags $PK) $T/directormanager_test.cpp $D/directormanager.cpp \
    $D/directorengine.cpp moc_directormanager.cpp -o manager_test $ENGINE_LIB \
    $(pkg-config --libs Qt6Core Qt6Gui Qt6Test Qt6Widgets) 2>&1 | grep "error"
[ -x manager_test ] || fail "manager test build"
QT_QPA_PLATFORM=offscreen timeout 120 ./manager_test 2>&1 | grep "FAIL\|Loc:\|Totals\|signal"
[ "${PIPESTATUS[0]}" = "0" ] || fail "manager test"

echo "== End to end: real Stage Wizard + Director + panel, inside the application"
cd "$BUILD" || exit 1
COMPILE=$(ninja -t commands qlcplus5 | grep "qlcplus5.dir/app.cpp.o " | head -1 | \
    sed "s#qmlui/CMakeFiles/qlcplus5.dir/app.cpp.o#$WORK/e2e.o#g; s#$SRC/qmlui/app.cpp#$T/director_e2e.cpp#g")
bash -c "$COMPILE" 2>&1 | grep "error"
LINK=$(ninja -t commands qlcplus5 | tail -1 | \
    sed "s#qmlui/CMakeFiles/qlcplus5.dir/main.cpp.o#$WORK/e2e.o#g; s#-o qmlui/qlcplus5#-o $WORK/e2e#g")
bash -c "$LINK" 2>&1 | grep "error"
[ -x "$WORK/e2e" ] || fail "end to end build"
(cd "$BUILD/qmlui" && QT_QPA_PLATFORM=offscreen timeout 60 "$WORK/e2e" > "$WORK/e2e.log" 2>&1) || fail "end to end run (see $WORK/e2e.log)"
grep "^FAIL\|E2E\|  layer\|signal" "$WORK/e2e.log"

echo "== Panel: QML warnings and screenshot"
cd "$WORK" || exit 1
mkdir -p panel && cp $SRC/qmlui/qml/*.qml $SRC/qmlui/qml/qmldir $SRC/qmlui/qml/director/DirectorPanel.qml panel/
g++ -g -std=c++17 -fPIC -DQMLUI $INC $(pkg-config --cflags $PK) $T/directorpanel_preview.cpp $D/directormanager.cpp \
    $D/directorengine.cpp moc_directormanager.cpp -o panel_preview $ENGINE_LIB \
    $(pkg-config --libs Qt6Core Qt6Gui Qt6Quick Qt6Qml) 2>&1 | grep "error"
[ -x panel_preview ] || fail "panel preview build"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software timeout 60 ./panel_preview panel/DirectorPanel.qml "$WORK/panel.png" 2>&1 | grep -v "$NOISE"
[ "${PIPESTATUS[0]}" = "0" ] || fail "panel preview"

echo
if [ "$FAILED" = "0" ]; then echo "ALL DIRECTOR CHECKS PASSED"; else echo "SOME DIRECTOR CHECKS FAILED"; fi
exit $FAILED
