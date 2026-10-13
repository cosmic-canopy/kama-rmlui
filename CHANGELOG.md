# Changelog

All notable changes to `@kama/rmlui`. The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
package follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] — unreleased

The first version, made general from the UI shim the-mallow-man (E7b/E7c, 2026-10-07) and friendly-fire-department
(a676ca3) had built for their menus. README.md, "Where it came from", lists what changed and why.

- RmlUi 6.3 and FreeType 2.14.3, fetched by SHA and built static by `native/Makefile`. The game's switches come along:
  minimal FreeType, `RMLUI_STATIC_LIB`, GNU compilers on Windows, a compiler stamp.
- A recording render interface: draws, scissors, transforms and clip masks, read back per context.
- Contexts, documents from a file or from RML text, fonts from a file or from bytes, and provided images.
- Elements as handles tracked by `ObserverPtr`, dead rather than dangling. Bindings: an id held across rewrites.
- Listeners on any event and any element, queued per context and polled. Each event carries its target and its
  parameters.
- RmlUi's log as a queue as well as stdout.
- `tests/`: 91 checks, debug and release, clean under AddressSanitizer.
