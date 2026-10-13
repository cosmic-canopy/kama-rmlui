#!/bin/sh
# test.sh — build the package's tests (tests/, one program) debug and release, and run them. `kama` is taken from
# $KAMA or the PATH. Runs from the package root, which is where tests/kama.json's `-L` paths and the test's font and
# fixture paths are relative to.
#
# Needs the native archives first:  tools/fetch-rmlui.sh && tools/fetch-freetype.sh && make -C native
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KAMA=${KAMA:-kama}
cd "$ROOT"
PLAT=$(tools/platform)
TARGET=$(tools/platform target)
[ -f "native/build/$PLAT/lib/librmlui.a" ] || {
    echo "test.sh: no native/build/$PLAT/lib/librmlui.a — run tools/fetch-rmlui.sh, tools/fetch-freetype.sh, make -C native" >&2
    exit 1
}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

"$KAMA" pkg install tests/kama.json >/dev/null
for mode in --debug --release; do
    "$KAMA" build tests/kama.json --target "$TARGET" $mode -o "$tmp/tests$mode"
    "$tmp/tests$mode"
done
echo "test.sh: OK"
