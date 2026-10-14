#!/bin/sh
# test.sh — build the package's tests (tests/, one program) and run them: debug, release, and debug under
# AddressSanitizer with leak checking (Linux; ASAN=0 skips it). `kama` is taken from $KAMA or the PATH. Runs from the
# package root, which the tests' font and fixture paths are relative to.
#
# RmlUi is vendored (third_party/rmlui) and compiled by kama, and FreeType comes from @kama/freetype beside this
# repository (../kama-freetype). There is nothing to fetch or build first. There is no wasm leg: the package is
# `!ARCH_WASM32` throughout. @kama/ui's tools/test-web.sh shows the gates hold in a wasm build that depends on it.
#
# AddressSanitizer is a compiler wrapper rather than a manifest entry: kama.json has no per-build-type `cflags`, and a
# `select.TARGET` entry needs a fixed triple. So `cc`/`c++` are wrapped to add the flag to every compile and the link.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KAMA=${KAMA:-kama}
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
cd "$ROOT"

"$KAMA" pkg install kama.json >/dev/null
"$KAMA" check kama.json
"$KAMA" pkg install tests/kama.json >/dev/null
for mode in --debug --release; do
    "$KAMA" build tests/kama.json $mode -o "$tmp/tests$mode"
    "$tmp/tests$mode"
done

if [ "$(uname -s)" = Linux ] && [ "${ASAN:-1}" != 0 ]; then
    printf '#!/bin/sh\nexec cc -fsanitize=address -fno-omit-frame-pointer "$@"\n' > "$tmp/cc-asan"
    printf '#!/bin/sh\nexec c++ -fsanitize=address -fno-omit-frame-pointer "$@"\n' > "$tmp/cxx-asan"
    chmod +x "$tmp/cc-asan" "$tmp/cxx-asan"
    "$KAMA" build tests/kama.json --debug --no-cache --cc "$tmp/cc-asan" --cxx "$tmp/cxx-asan" -o "$tmp/tests-asan"
    ASAN_OPTIONS=detect_leaks=1 "$tmp/tests-asan"
    echo "test.sh: AddressSanitizer clean"
fi
echo "test.sh: OK"
