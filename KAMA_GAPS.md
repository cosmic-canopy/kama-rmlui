# Gaps found in kama, from building @kama/rmlui

Found while lifting the RmlUi binding out of the-mallow-man and friendly-fire-department (2026-10-13). Each was run on
the version named. Each gap asks **Open: language, std or package?** (here mostly the toolchain's manifest).

**Current compiler:** `kama 0.9.577+g660b7812` (`../kama`).

---

## OPEN

### 1. A dependency's `link` cannot be gated on a fact (GAP)

A package's `"select": {"TARGET": {"WASM": {"link": []}}}` arm applies only to a target NAMED `WASM`. A consumer that
names its wasm target anything else gets the native libraries linked into WebAssembly.

Repro: a package with `"link": ["nosuchlib"]` and `select.TARGET.WASM.link: []`; a consumer with a target
`"WEB": {"triple": "wasm32-emscripten-none"}`. `--target WASM` builds; `--target WEB` fails with
`wasm-ld: unable to find library -lnosuchlib`. Suggestion: `{name, compileFor}` entries in `link`, as `csources`
already allows. **Open: language, std or package?** The toolchain (manifest).

### 2. A default build does not pick the OS arm (GAP)

With no `--target`, a Linux build applies `select.TARGET.HOST` and never the `LINUX` arm (checked with `-include`
markers), so per-OS `-L` paths must be written under both `HOST` and `LINUX`. **Open: language, std or package?** The
toolchain (manifest).

### 3. A gated-out `cincludes` path must still exist (friction, documented)

kama checks a gated entry's path even when the gate excludes it, so a web-only consumer must still fetch RmlUi for its
headers to exist on disk. Documented behaviour; a gated-out path could be skipped. **Open: language, std or package?**
The toolchain.
