# @kama/rmlui: what is true in THIS repo

`AGENTS.md` (the language) and `AGENTS.package.md` (a publishable library) are written by `kama agents install` and are
not hand-edited. This file is the project-specific third, and it is the one to edit. README.md holds the design. These
are the working rules.

- **`tools/vendor-rmlui.sh` owns three things.** It writes `third_party/rmlui/`, `csrc/rmlui/` (one wrapper per
  RmlUi source, plus `kama_rmlui_build.h`), and the `csources` block of `kama.json`. None of them is edited by hand.
  To bump RmlUi, change `VERSION`, `COMMIT` and `SHA256`, run the script, read the licence gate's verdict and the
  diff, run the tests, and bump this package's version. The gate is **MIT only**: never vendor a file under any other
  licence.
- **RmlUi's build definitions never go in `cflags`.** They live in the generated `csrc/rmlui/kama_rmlui_build.h`, which
  only the wrappers include. A dependency's `cflags` reach every translation unit of the consumer. Every `csources`
  entry is gated `!ARCH_WASM32`.
- **FreeType is `@kama/freetype`** (`../kama-freetype`, by path until it is on the registry). `tests/kama.json`
  restates that path, because kama requires every consumer to list its dependencies' path dependencies. Change
  FreeType's modules or options there, not here.
- **Run `tools/test.sh`** (`KAMA=/path/to/kama` if `kama` is not on the PATH). It builds `tests/` in debug, in release,
  and in debug under AddressSanitizer, from the package root. The ASan leg wraps `cc`/`c++`, so RmlUi, FreeType and
  the shim are all instrumented. A failing case prints its name. There is no wasm leg: the package is
  `!ARCH_WASM32` throughout. `@kama/ui`'s `tools/test-web.sh` is what shows the gates hold in a wasm build that
  depends on this package.
- **`csrc/kama_rmlui.cpp` is the only hand-written file that includes RmlUi.** `csrc/kama_rmlui.h` is the whole C ABI.
  `src/rmlui.kama` is the only kama file that `extern`s it, and it mirrors every prototype. kama emits no prototype
  for an `extern`, so a read-only direction gotten wrong is silent. Keep `const char*` as `UnsafeConstPtr<cchar>` and
  `const T*` as `UnsafeConstPtr<T>`.
- **Every number kama spells is `static_assert`ed in the shim**: key identifiers, modifiers, box areas, show flags,
  and the matrix layout. A new constant in `src/keys.kama` gets its assert in the same change.
- **Handles are values; lifetimes are explicit.** Do not add a kama `resource` that owns an RmlUi object: the UI seam
  above (`@kama/ui`) must stay a `value`, so that programs can keep it in a module `static`. Every shim entry point
  must answer a dead or unknown handle with its "none" value. The tests check that after a rewrite, a close, a
  destroy and a shutdown.
- **Licences**: `licenses/manifest.tsv` points at the vendored tree's own texts (RmlUi's `LICENSE.txt` and the
  containers' `LICENSE.txt`). FreeType's texts are `@kama/freetype`'s.
