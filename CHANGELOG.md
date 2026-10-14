# Changelog

All notable changes to `@kama/rmlui`. The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
package follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] — unreleased

The first version, made general from the UI shim the-mallow-man (E7b/E7c, 2026-10-07) and friendly-fire-department
(a676ca3) had built for their menus. README.md, "Where it came from", lists what changed and why.

- RmlUi 6.3 is vendored by `tools/vendor-rmlui.sh`, pinned by commit and SHA-256, and compiled by kama.
  - Only `Source/Core` and `Include` (Core and Config) are kept, with both MIT notices.
  - The licence gate is MIT only. It checks 442 files and keeps 442.
  - Each source compiles through a generated wrapper (`csrc/rmlui/`), so RmlUi's build definitions never reach a
    consumer. Every entry is gated `!ARCH_WASM32`.
  - `RMLUI_STATIC_LIB` is defined by the shim and the wrappers.
- FreeType comes from `@kama/freetype` (a path dependency until the registry has it). The fetch scripts,
  `native/Makefile`, the CMake build, `link: [rmlui, freetype]` and the consumer's `-L` paths are gone.
- A recording render interface: draws, scissors, transforms and clip masks, read back per context.
- Contexts, documents from a file or from RML text, fonts from a file or from bytes, and provided images.
- Elements as handles tracked by `ObserverPtr`, dead rather than dangling. Bindings: an id held across rewrites.
- Listeners on any event and any element, queued per context and polled. Each event carries its target and its
  parameters.
- RmlUi's log as a queue as well as stdout.
- `tests/`: 91 checks, in debug, in release, and under AddressSanitizer with leak checking. `tools/test.sh` now
  runs the ASan leg itself. The test font (Lato, OFL) is committed under `tests/fonts/`.
