# Gaps found in kama, from building @kama/rmlui

Found while lifting the RmlUi binding out of the-mallow-man and friendly-fire-department (2026-10-13), and while
vendoring RmlUi for 0.1.0 (the same day). Each was run on the version named. Each gap asks **Open: language, std or
package?** (here mostly the toolchain's manifest).

**Current compiler:** `kama 0.9.577+g660b7812` (`../kama`).

---

## OPEN

### 4. A dependency entry accepts any key, and cannot be gated to the targets that use it (BUG + GAP)

**The bug.** `kama.json` refuses unknown keys elsewhere. For example, ``unknown key `cflags` in
`select.BUILD_TYPE.ASAN` ``. But a dependency entry accepts anything, silently.

Repro: an executable with `"dependencies": {"@kama/freetype": {"path": "../kama-freetype", "bogus": 1}}`.
`kama pkg install` and `kama check` both pass, with no word about `bogus`.

**The gap.** `@kama/rmlui` gates every source on `!ARCH_WASM32`, but its dependency `@kama/freetype` cannot be gated
the same way. Written `{"path": …, "compileFor": ["!ARCH_WASM32"]}`, the gate is accepted (the bug above) and
ignored. A `--target WASM` build still compiles every FreeType wrapper unit (their objects are in `.kama-cache/`). So a wasm
build of anything above this package (`@kama/ui`'s web leg) compiles FreeType's 22 translation units for nothing.
The link drops the code, but every cold build pays the compile.

Suggestion: refuse unknown keys in a dependency entry, and take `compileFor` there, judged like a `csources`
entry's. **Open: language, std or package?** The toolchain (manifest).

### 5. A sanitizer build has nowhere to live in the manifest (GAP)

This is the same gap as `@kama/freetype`'s KAMA_GAPS #1. `select.BUILD_TYPE` values take no `cflags`, and a
`select.TARGET` entry needs a fixed `triple`. Until 0.1.0 the ASan run used a hand-written `tests/kama.local.json`.
`tools/test.sh` now wraps `cc`/`c++` with `-fsanitize=address` and passes them as `--cc`/`--cxx`. **Open: language,
std or package?** The toolchain (manifest).

---

## NO LONGER HIT HERE — kept for the record

The 0.1.0 draft linked CMake-built archives, and it hit gaps 1 to 3. Vendoring RmlUi removed the `link` list and
every `-L` path, and put the include tree in the repository. So this package no longer hits them, but they are still
gaps in kama.

### 1. A dependency's `link` cannot be gated on a fact (GAP)

A package's `"select": {"TARGET": {"WASM": {"link": []}}}` arm applies only to a target NAMED `WASM`. A consumer that
names its wasm target anything else gets the native libraries linked into WebAssembly.

Repro: a package with `"link": ["nosuchlib"]` and `select.TARGET.WASM.link: []`; a consumer with a target
`"WEB": {"triple": "wasm32-emscripten-none"}`. `--target WASM` builds; `--target WEB` fails with
`wasm-ld: unable to find library -lnosuchlib`. Suggestion: `{name, compileFor}` entries in `link`, as `csources`
already allows. **Open: language, std or package?** The toolchain (manifest).

### 2. A default build does not pick the OS arm (GAP)

With no `--target`, a Linux build applies `select.TARGET.HOST` and never the `LINUX` arm (checked with `-include`
markers). So per-OS `-L` paths had to be written under both `HOST` and `LINUX`. **Open: language, std or package?**
The toolchain (manifest).

### 3. A gated-out `cincludes` path must still exist (friction, documented)

kama checks a gated entry's path even when the gate excludes it. So a web-only consumer had to fetch RmlUi for its
headers to exist on disk. This is documented behaviour, but a gated-out path could be skipped. **Open: language, std
or package?** The toolchain.
