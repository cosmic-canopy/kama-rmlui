#!/bin/sh
# vendor-rmlui.sh — pin one RmlUi release into third_party/rmlui/, run the licence gate over it, and regenerate what is
# derived from it: the wrapper translation units in csrc/rmlui/ (with their build header) and the `csources` block of
# kama.json.
#
# Vendored on purpose. A consumer of @kama/rmlui gets a working build with nothing to fetch, nothing to build with
# CMake and no `-L` path to set. kama compiles RmlUi into the program the way it compiles any package's sources, with
# the consumer's own compiler. Until 0.1.0 this package fetched RmlUi and FreeType by git SHA and built both with CMake
# (native/Makefile), and every consumer pointed its `ldflags` at the archives. FreeType is now its own package
# (@kama/freetype, a dependency), and RmlUi is here. Upgrading is a change to VERSION, COMMIT and SHA256, a run, a
# read of the diff, and a version bump.
#
# The archive GitHub serves for the pinned COMMIT (tag 6.3, a lightweight tag), not the tag's archive. A tag can
# move, and the commit's archive names the commit in its top directory. Its kept files are byte-identical to the
# git checkout the old fetch script made (compared when this pin was set).
#
# What lands, each file byte-identical to the archive:
#   third_party/rmlui/LICENSE.txt                         RmlUi's MIT notice
#   third_party/rmlui/Include/RmlUi/Core.h, Config/, Core/   the public headers, with Core/Containers/LICENSE.txt
#                                                         (robin_hood's and itlib's MIT notices)
#   third_party/rmlui/Source/Core/                        the core library: its .cpp, .h and .inl files
# What does NOT: Samples, Backends, Tests, Utilities, Dependencies, CMake, and the Debugger, Lua, SVG and Lottie
# plugins (sources and headers). The font engine compiled is RmlUi's default, FreeType, which comes from the
# @kama/freetype dependency.
#
# THE LICENCE GATE: MIT only. RmlUi 6 carries its licence in LICENSE.txt rather than in each file, so a file that
# names no licence is covered by it. A file that does name one must name MIT (SPDX-License-Identifier: MIT, or the
# MIT grant), and any other licence named in a kept file, any copyleft text, or a LICENSE.txt that is no longer the
# MIT text FAILS the run. So an upgrade cannot bring a new licence in unread.
#
# RmlUi's build must not leak into the consumer. RmlUi compiles with RMLUI_VERSION, RMLUI_FONT_ENGINE_FREETYPE and
# RMLUI_STATIC_LIB (its CMake's PRIVATE and PUBLIC definitions for a static rmlui_core). A `cflags` entry would reach
# every translation unit of every consumer, kama's own C included. So each RmlUi source is compiled through a
# WRAPPER in csrc/rmlui/ that includes csrc/rmlui/kama_rmlui_build.h and then the pristine .cpp. The shim
# (csrc/kama_rmlui.cpp) defines RMLUI_STATIC_LIB itself, the one public definition. Only csrc/ and csrc/rmlui/ hold
# sources, so they are the only directories kama adds to every translation unit's include path. RmlUi's own Source/
# directories are never on it, and their headers (Pool.h, Clock.h, Template.h …) cannot shadow a consumer's.
#
# Every entry is gated `!ARCH_WASM32`: RmlUi is the native leg of @kama/ui, and a browser build uses the DOM instead.
#
# Generated here, never hand-edited: everything under third_party/rmlui/ and csrc/rmlui/, and the `csources` block of
# kama.json (from its `"csources": [` line to the `]` that closes it). Needs curl, tar, python3.
set -eu

VERSION=6.3
COMMIT=ba95ffe8bfb6370efb2cdcca927eaad4710c5413   # refs/tags/6.3, a lightweight tag
SHA256=1541ef5577115e9368f8ed389b29f0925ef6572f326a33d378ea16c3cfa2cde8
URL="https://github.com/mikke89/RmlUi/archive/${COMMIT}.tar.gz"

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

# A local archive (checked against the same hash) skips the download.
tarball="${RMLUI_TARBALL:-}"
if [ -z "$tarball" ]; then
    tarball="$tmp/rmlui.tar.gz"
    echo "vendor-rmlui: fetching $URL"
    curl -fsSL -o "$tarball" "$URL"
fi
got=$(sha256 "$tarball")
if [ "$got" != "$SHA256" ]; then
    echo "vendor-rmlui: sha256 mismatch for RmlUi $VERSION ($COMMIT): got $got, want $SHA256" >&2
    exit 1
fi

mkdir "$tmp/x"
tar -xzf "$tarball" -C "$tmp/x"
src="$tmp/x/RmlUi-$COMMIT"
[ -f "$src/LICENSE.txt" ] && [ -d "$src/Source/Core" ] && [ -d "$src/Include/RmlUi/Core" ] || {
    echo "vendor-rmlui: unexpected archive layout" >&2; exit 1; }

python3 -I - "$src" "$ROOT" "$VERSION" "$URL" "$SHA256" "$COMMIT" <<'EOF'
import json, os, re, shutil, sys

src, root, version, url, sha, commit = sys.argv[1:7]
dest = os.path.join(root, "third_party", "rmlui")

def die(msg):
    sys.stderr.write("vendor-rmlui: " + msg + "\n")
    sys.exit(1)

def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()

def norm(text):
    return re.sub(r"\s+", " ", re.sub(r"[*/#]", " ", text)).lower()

# ---- the candidates ---------------------------------------------------------------------------------------------
SOURCE_EXT = (".cpp", ".h", ".inl", ".hpp")
candidates = ["Include/RmlUi/Core.h"]
for base in ("Source/Core", "Include/RmlUi/Core", "Include/RmlUi/Config"):
    for dp, _, fs in os.walk(os.path.join(src, base)):
        candidates += [os.path.relpath(os.path.join(dp, f), src) for f in fs if f.endswith(SOURCE_EXT)]
candidates = sorted(set(candidates))
licences = ["LICENSE.txt", "Include/RmlUi/Core/Containers/LICENSE.txt"]

# ---- the licence gate: MIT only ---------------------------------------------------------------------------------
MIT_GRANT = "permission is hereby granted, free of charge, to any person obtaining a copy"
OTHER = re.compile(r"apache licen[cs]e|\bbsd\b|boost software licen[cs]e|zlib licen[cs]e|public domain|unlicense|"
                   r"\bisc licen[cs]e|creative commons|\bcc0\b|open font licen[cs]e|wtfpl|\bcddl\b|"
                   r"eclipse public licen[cs]e|mozilla public licen[cs]e|\bmpl\b")
COPYLEFT = re.compile(r"gnu (lesser |library |affero )?general public licen[cs]e|\b[al]?gplv?[23]?\b|copyleft|"
                      r"share ?alike|european union public licen[cs]e|\beupl\b")
failures, kept, named = [], [], []
for p in candidates:
    n = norm(read(os.path.join(src, p)))
    for spdx in re.findall(r"spdx-license-identifier: ?([a-z0-9.+-]+)", n):
        if spdx != "mit":
            failures.append("%s: SPDX-License-Identifier %s" % (p, spdx))
    m = OTHER.search(n) or COPYLEFT.search(n)
    if m:
        failures.append("%s: names a licence other than MIT (%r)" % (p, m.group(0)))
        continue
    if "licen" in n and ("spdx-license-identifier: mit" in n or MIT_GRANT in n):
        named.append(p)
    elif re.search(r"licen[cs]e[ds]? (under|to)|under the .{0,40}licen[cs]e", n):
        failures.append("%s: states a licence the gate does not recognise as MIT: read it" % p)
        continue
    kept.append(p)
root_lic = norm(read(os.path.join(src, "LICENSE.txt")))
if not root_lic.startswith("mit license") or MIT_GRANT not in root_lic:
    failures.append("LICENSE.txt is no longer the MIT licence: read it")
containers = read(os.path.join(src, licences[1]))
if "all MIT licensed" not in containers or OTHER.search(norm(containers)) or COPYLEFT.search(norm(containers)):
    failures.append("%s no longer lists MIT licences only: read it" % licences[1])
if failures:
    die("licence gate FAILED:\n  " + "\n  ".join(failures))

# ---- copy, pristine ---------------------------------------------------------------------------------------------
if os.path.isdir(dest):
    shutil.rmtree(dest)
for p in kept + licences:
    os.makedirs(os.path.dirname(os.path.join(dest, p)), exist_ok=True)
    shutil.copyfile(os.path.join(src, p), os.path.join(dest, p))

# ---- the wrappers: one per RmlUi source, csrc/rmlui/rml_<dir>_<name>.cpp ----------------------------------------
wdir = os.path.join(root, "csrc", "rmlui")
if os.path.isdir(wdir):
    shutil.rmtree(wdir)
os.makedirs(wdir)
with open(os.path.join(wdir, "kama_rmlui_build.h"), "w") as f:
    f.write("""// GENERATED by tools/vendor-rmlui.sh. Do not edit: re-run the script.
// How @kama/rmlui compiles RmlUi %s: the definitions RmlUi's own CMake gives a static rmlui_core with the FreeType font
// engine. Included FIRST by every wrapper in this directory, and by nothing else, so a consumer's translation units
// never see the PRIVATE ones. RMLUI_STATIC_LIB is RmlUi's one PUBLIC definition: csrc/kama_rmlui.cpp, the only other
// file that includes an RmlUi header, defines it too.
#ifndef KAMA_RMLUI_BUILD_H
#define KAMA_RMLUI_BUILD_H

#ifndef RMLUI_STATIC_LIB
#define RMLUI_STATIC_LIB 1
#endif
#undef RMLUI_VERSION
#define RMLUI_VERSION "%s"
#define RMLUI_FONT_ENGINE_FREETYPE 1

// The plugins and the profiler are not vendored (tools/vendor-rmlui.sh, "What does NOT").
#undef RMLUI_SVG_PLUGIN
#undef RMLUI_LOTTIE_PLUGIN
#undef RMLUI_TRACY_PROFILING
#undef RMLUI_TRACY_MEMORY_PROFILING

#endif
""" % (version, version))
wrappers = []
for p in kept:
    if not p.endswith(".cpp"):
        continue
    rel = os.path.relpath(p, "Source")                      # Core/Context.cpp, Core/Elements/ElementForm.cpp
    name = "rml_" + rel[:-4].replace("/", "_") + ".cpp"
    with open(os.path.join(wdir, name), "w") as f:
        f.write("// GENERATED by tools/vendor-rmlui.sh from RmlUi %s's %s. Do not edit: re-run the script.\n"
                "#include \"kama_rmlui_build.h\"\n#include \"../../third_party/rmlui/%s\"\n" % (version, p, p))
    wrappers.append("csrc/rmlui/" + name)
if len(set(os.path.basename(w) for w in wrappers)) != len(wrappers):
    die("two wrappers share a name")

# ---- VENDORED ---------------------------------------------------------------------------------------------------
with open(os.path.join(dest, "VENDORED"), "w") as f:
    f.write("RmlUi %s\n%s\nsha256 %s\ngit: tag %s = commit %s\n\n" % (version, url, sha, version, commit))
    f.write("Licence: MIT (LICENSE.txt). RmlUi's containers, robin_hood and itlib, are MIT under their own notices\n"
            "(Include/RmlUi/Core/Containers/LICENSE.txt). Both notices ship with every native build.\n\n")
    f.write("Licence gate: %d candidate files checked, %d kept, 0 refused. %d name MIT themselves; the rest name no\n"
            "licence and are covered by LICENSE.txt.\n\n" % (len(candidates), len(kept), len(named)))
    f.write("Kept, byte-identical (.cpp/.h/.inl/.hpp, plus the two licence files): Source/Core, Include/RmlUi/Core.h,\n"
            "Include/RmlUi/Core, Include/RmlUi/Config. %d sources, one wrapper each in csrc/rmlui/.\n\n" % len(wrappers))
    f.write("Not vendored: Samples, Backends, Tests, Utilities, Dependencies, CMake, and the Debugger, Lua, SVG and\n"
            "Lottie plugins. The FreeType font engine compiles against @kama/freetype.\n")

# ---- kama.json: the csources block ------------------------------------------------------------------------------
entries = ["csrc/kama_rmlui.cpp"] + sorted(wrappers)
path = os.path.join(root, "kama.json")
lines = open(path).read().split("\n")
try:
    start = next(i for i, l in enumerate(lines) if l == '  "csources": [')
    end = next(i for i in range(start + 1, len(lines)) if lines[i].startswith("  ]"))
except StopIteration:
    die('kama.json has no `  "csources": [` ... `  ]` block to regenerate')
block = ['    { "path": "%s", "compileFor": ["!ARCH_WASM32"] }%s' % (e, "," if k < len(entries) - 1 else "")
         for k, e in enumerate(entries)]
text = "\n".join(lines[:start + 1] + block + lines[end:])
json.loads(text)
open(path, "w").write(text)

print("vendor-rmlui: licence gate: %d checked, %d kept (%d naming MIT, the rest under LICENSE.txt), 0 refused"
      % (len(candidates), len(kept), len(named)))
print("vendor-rmlui: RmlUi %s in third_party/rmlui; %d wrappers in csrc/rmlui; kama.json csources regenerated"
      % (version, len(wrappers)))
EOF
