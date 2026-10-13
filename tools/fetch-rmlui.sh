#!/usr/bin/env bash
# Fetch RmlUi into third_party/rmlui/: the library this package binds.
#
#   tools/fetch-rmlui.sh            # fetch at the pin below, or verify an existing checkout
#   tools/fetch-rmlui.sh --force    # re-fetch over an existing checkout
#
# From the-mallow-man's tools/fetch-rmlui.sh (same version, same posture), itself from
# friendly-fire-department@a676ca3: pinned by COMMIT and verified, with the tag only as a cross-check — a tag can
# move. SOURCE ONLY and not per-platform; `make -C native` builds it into native/build/<plat>/, which is.
# Fetched rather than committed (~10 MB of C++; the real pin is the SHA below); third_party/ is gitignored.
#
# ⚠️ **RmlUi 6.x IS THE LINE csrc/kama_rmlui.cpp IS WRITTEN AGAINST** (the Span-based RenderInterface, clip masks,
# `SetTransform`); 5.x has a different geometry API, so moving this pin backwards is not free.
#
# Licence: MIT, notice only. ⚠️ **TWO TEXTS, NOT ONE**: RmlUi vendors robin_hood and itlib under their own MIT
# notices (Include/RmlUi/Core/Containers/LICENSE.txt), and friendly-fire-department's FR-28 audit found 472 of their
# symbols in a shipped binary against a notice that named neither. Both texts are COMMITTED in licenses/; this
# script compares them with the pinned tree and WARNS on a difference (never fails, never overwrites).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/third_party/rmlui"

RMLUI_REPO="${RMLUI_REPO:-https://github.com/mikke89/RmlUi.git}"
RMLUI_TAG="6.3"
RMLUI_SHA="ba95ffe8bfb6370efb2cdcca927eaad4710c5413"   # refs/tags/6.3, checked 2026-10-07 (a lightweight tag)
STAMP=.fetch-rmlui-pin

FORCE=0
for a in "$@"; do
  case "$a" in
    --force) FORCE=1 ;;
    *) echo "fetch-rmlui: unknown argument '$a'" >&2; exit 2 ;;
  esac
done

command -v git >/dev/null 2>&1 || { echo "fetch-rmlui: need git on PATH" >&2; exit 1; }

have=""
[ -f "$DEST/$STAMP" ] && have="$(tr -d ' \t\r\n' < "$DEST/$STAMP")"
if [ "$have" = "$RMLUI_SHA" ] && [ "$FORCE" -eq 0 ]; then
  echo "fetch-rmlui: RmlUi $RMLUI_TAG already at ${RMLUI_SHA:0:12} — pass --force to re-fetch"
else
  [ -n "$have" ] && echo "fetch-rmlui: have ${have:0:12}, the pin is ${RMLUI_SHA:0:12} — re-fetching" >&2
  rm -rf "$DEST"
  mkdir -p "$DEST"
  echo "fetch-rmlui: $RMLUI_REPO @ $RMLUI_TAG = ${RMLUI_SHA:0:12} -> $DEST"
  git -C "$DEST" init -q
  git -C "$DEST" remote add origin "$RMLUI_REPO"
  git -C "$DEST" -c advice.detachedHead=false fetch -q --depth 1 origin "$RMLUI_SHA"
  git -C "$DEST" -c advice.detachedHead=false checkout -q FETCH_HEAD
  got="$(git -C "$DEST" rev-parse HEAD)"
  if [ "$got" != "$RMLUI_SHA" ]; then
    echo "fetch-rmlui: got $got, expected $RMLUI_SHA — refusing" >&2
    rm -rf "$DEST"
    exit 1
  fi
  tagsha="$(git ls-remote "$RMLUI_REPO" "refs/tags/$RMLUI_TAG^{}" | cut -f1)"
  [ -n "$tagsha" ] || tagsha="$(git ls-remote "$RMLUI_REPO" "refs/tags/$RMLUI_TAG" | cut -f1)"
  if [ -n "$tagsha" ] && [ "$tagsha" != "$RMLUI_SHA" ]; then
    echo "fetch-rmlui: WARN upstream tag $RMLUI_TAG now points at $tagsha, not the pinned $RMLUI_SHA" >&2
  fi
  rm -rf "$DEST/.git"
  printf '%s\n' "$RMLUI_SHA" > "$DEST/$STAMP"
fi

licence() {
  local src="$1" have="$2"
  if [ ! -f "$src" ]; then echo "  WARN  licences: $src is missing from the fetched tree" >&2; return 0; fi
  if [ ! -f "$have" ]; then
    echo "  WARN  licences: licenses/$(basename "$have") is not committed — copy it from $src" >&2
  elif cmp -s "$src" "$have"; then
    echo "  ok    licences: $(basename "$have") matches the pinned tree"
  else
    echo "  WARN  licences: licenses/$(basename "$have") differs from the pinned tree's text" >&2
  fi
}
licence "$DEST/LICENSE.txt"                               "$ROOT/licenses/RmlUi-MIT.txt"
licence "$DEST/Include/RmlUi/Core/Containers/LICENSE.txt" "$ROOT/licenses/RmlUi-containers-MIT.txt"

echo "fetch-rmlui: ok — next: tools/fetch-freetype.sh, then make -C native"
