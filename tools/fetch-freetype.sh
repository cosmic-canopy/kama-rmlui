#!/usr/bin/env bash
# Fetch FreeType into third_party/freetype/: RmlUi's font engine, built static by `make -C native`.
#
#   tools/fetch-freetype.sh            # fetch at the pin below, or verify an existing checkout
#   tools/fetch-freetype.sh --force    # re-fetch over an existing checkout
#
# From the-mallow-man's tools/fetch-freetype.sh (same version), itself from friendly-fire-department@a676ca3:
# pinned by COMMIT, with the tag as a cross-check.
#
# ⚠️ **WHY BUILD FREETYPE AT ALL RATHER THAN TAKE THE PLATFORM'S** (friendly-fire-department measured it, FR-4):
# MSYS2's FreeType is built with every optional feature on and drags fourteen more DLLs into an .exe — harfbuzz,
# graphite2, brotli, bz2, png, zlib, pcre2, plus glib/intl/iconv, which are LGPL — and harfbuzz loads the very C++
# runtime DLLs a static Windows link keeps out. RmlUi uses FreeType only to rasterise plain TrueType/OpenType
# glyphs, so native/Makefile builds this source with HarfBuzz, Brotli, BZip2, PNG and system zlib all OFF and links
# it static: no DLL, and the licence stays FTL. One pinned rasteriser on every platform also means a glyph is the
# same pixels on every machine.
#
# Licence: FreeType is dual FTL / GPLv2; this package ELECTS the FTL, never GPLv2. The FTL asks for a credit line in
# the documentation of a product that ships it (README.md, "What a program that ships this must carry"). ⚠️ The
# build compiles in FreeType's own copy of zlib's inflate (src/gzip/, zlib licence), which asks nothing of a binary.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/third_party/freetype"

FREETYPE_REPO="${FREETYPE_REPO:-https://github.com/freetype/freetype.git}"
FREETYPE_TAG="VER-2-14-3"
FREETYPE_SHA="0a0221a1347e2f1e07c395263540026e9a0aa7c7"   # refs/tags/VER-2-14-3^{}, checked 2026-10-07
STAMP=.fetch-freetype-pin

FORCE=0
for a in "$@"; do
  case "$a" in
    --force) FORCE=1 ;;
    *) echo "fetch-freetype: unknown argument '$a'" >&2; exit 2 ;;
  esac
done

command -v git >/dev/null 2>&1 || { echo "fetch-freetype: need git on PATH" >&2; exit 1; }

have=""
[ -f "$DEST/$STAMP" ] && have="$(tr -d ' \t\r\n' < "$DEST/$STAMP")"
if [ "$have" = "$FREETYPE_SHA" ] && [ "$FORCE" -eq 0 ]; then
  echo "fetch-freetype: FreeType $FREETYPE_TAG already at ${FREETYPE_SHA:0:12} — pass --force to re-fetch"
else
  [ -n "$have" ] && echo "fetch-freetype: have ${have:0:12}, the pin is ${FREETYPE_SHA:0:12} — re-fetching" >&2
  rm -rf "$DEST"
  mkdir -p "$DEST"
  echo "fetch-freetype: $FREETYPE_REPO @ $FREETYPE_TAG = ${FREETYPE_SHA:0:12} -> $DEST"
  git -C "$DEST" init -q
  git -C "$DEST" remote add origin "$FREETYPE_REPO"
  git -C "$DEST" -c advice.detachedHead=false fetch -q --depth 1 origin "$FREETYPE_SHA"
  git -C "$DEST" -c advice.detachedHead=false checkout -q FETCH_HEAD
  got="$(git -C "$DEST" rev-parse HEAD)"
  if [ "$got" != "$FREETYPE_SHA" ]; then
    echo "fetch-freetype: got $got, expected $FREETYPE_SHA — refusing" >&2
    rm -rf "$DEST"
    exit 1
  fi
  tagsha="$(git ls-remote "$FREETYPE_REPO" "refs/tags/$FREETYPE_TAG^{}" | cut -f1)"
  if [ -n "$tagsha" ] && [ "$tagsha" != "$FREETYPE_SHA" ]; then
    echo "fetch-freetype: WARN upstream tag $FREETYPE_TAG now points at $tagsha, not the pinned $FREETYPE_SHA" >&2
  fi
  rm -rf "$DEST/.git"
  printf '%s\n' "$FREETYPE_SHA" > "$DEST/$STAMP"
fi

if [ ! -f "$DEST/docs/FTL.TXT" ]; then
  echo "  WARN  licences: docs/FTL.TXT is missing from the fetched tree" >&2
elif [ ! -f "$ROOT/licenses/FreeType-FTL.txt" ]; then
  echo "  WARN  licences: licenses/FreeType-FTL.txt is not committed — copy it from $DEST/docs/FTL.TXT" >&2
elif cmp -s "$DEST/docs/FTL.TXT" "$ROOT/licenses/FreeType-FTL.txt"; then
  echo "  ok    licences: FreeType-FTL.txt matches the pinned tree"
else
  echo "  WARN  licences: licenses/FreeType-FTL.txt differs from the pinned tree's text" >&2
fi

echo "fetch-freetype: ok — next: make -C native"
