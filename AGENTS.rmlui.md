# @kama/rmlui: what is true in THIS repo

`AGENTS.md` (the language) and `AGENTS.package.md` (a publishable library) are written by `kama agents install` and are
not hand-edited. This file is the project-specific third, and it is the one to edit. README.md holds the design. These
are the working rules.

- **Build the native archives before anything links.** Run `tools/fetch-rmlui.sh`, then `tools/fetch-freetype.sh`,
  then `make -C native`. That gives `native/build/<plat>/lib/{librmlui,libfreetype}.a`. The fetch scripts own
  `third_party/` (pinned by SHA, gitignored). To bump a version, change the two constants in a script, run it, read
  the warnings, `rm -rf native/build/<plat>`, rebuild, and bump this package's version.
- **Run `tools/test.sh`** (`KAMA=/path/to/kama` if `kama` is not on the PATH). It builds `tests/` in debug and in
  release, from the package root. A failing case prints its name. There is no wasm leg: the package is
  `@compileFor(!ARCH_WASM32)` throughout. `@kama/ui`'s `tools/test-web.sh` is what shows the gates hold in a wasm
  build that depends on this package.
- **`csrc/kama_rmlui.cpp` is the only file that includes RmlUi.** `csrc/kama_rmlui.h` is the whole C ABI.
  `src/rmlui.kama` is the only kama file that `extern`s it, and it mirrors every prototype. kama emits no prototype
  for an `extern`, so a read-only direction gotten wrong is silent. Keep `const char*` as `UnsafeConstPtr<cchar>` and
  `const T*` as `UnsafeConstPtr<T>`.
- **Every number kama spells is `static_assert`ed in the shim**: key identifiers, modifiers, box areas, show flags,
  and the matrix layout. A new constant in `src/keys.kama` gets its assert in the same change.
- **Handles are values; lifetimes are explicit.** Do not add a kama `resource` that owns an RmlUi object: the UI seam
  above (`@kama/ui`) must stay a `value`, so that programs can keep it in a module `static`. Every shim entry point
  must answer a dead or unknown handle with its "none" value. The tests check that after a rewrite, a close, a
  destroy and a shutdown.
- **Run the shim under AddressSanitizer after any change to `csrc/`.** Put
  `{ "select": { "TARGET": { "LINUX": { "cflags": ["-fsanitize=address"], "ldflags": ["-Lnative/build/linux/lib", "-fsanitize=address"] } } } }`
  in `tests/kama.local.json` (gitignored), then build and run `tests/` with `--target LINUX`.
- **Licences**: the three texts in `licenses/` are committed and listed in `licenses/manifest.tsv`. The fetch scripts
  warn when a pinned tree's text differs from them.
