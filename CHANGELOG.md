# Changelog

All notable changes to `@kama/rmlui`. The format is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
package follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] — unreleased

What `@kama/ui` 0.3.0's native leg needs, in one release. Additions, plus fixes for a `<select>`. No existing signature
changes.

### Added

- `Element.appendRml(rml:) -> int32`: parses markup into new children after the existing ones. It returns how many
  were added. Existing children keep their handles, listeners and bindings, the scroll offset stays, and the cost is
  the new markup's only (RmlUi's own `Factory::InstanceElementText`, the parser `SetInnerRML` runs).
- `Element.removeChildren(from:, count:) -> int32` (clamped; returns how many were removed) and `Element.remove() ->
  bool`. A document is closed, never removed, and the context's root takes neither call.
- The clipboard, pulled: `RmlUi.provideClipboard(text:)` (what a paste reads), `RmlUi.copyText(text:)` and
  `RmlUi.takeClipboardWrite() -> Optional<string>`, which pops each copy, cut or `copyText` for the OS clipboard. The
  shim's system interface now overrides `GetClipboardText` / `SetClipboardText`. Before, RmlUi's fields copied into a
  private string.
- `RmlUi.eventElement(name:) -> Optional<Element>`: the drag events' `drag_element`. The shim turns that pointer into
  a handle when it queues the event, and drops any other raw pointer parameter, so a later read never dangles.
- `RmlUi.eventModifiers() -> int32`: the last event's `MOD_*` mask, from RmlUi's `ctrl_key` … `scroll_lock_key`
  parameters.
- Docs for drag and drop (`drag: drag-drop` or `clone`; `dragdrop` bubbles with the target and `drag_element`),
  double clicks (RmlUi's clock), keys, and the parts a tooltip is built from (`pointer-events: none`).

### Fixed

- A `<select>`'s children are its options. `childCount` and `child` read them, since RmlUi keeps options out of the
  DOM; before, both answered 0 once RmlUi had moved the options into the drop-down.
- `setInnerRml` and `setText` on a `<select>` replace its options. Before, RmlUi's `SetInnerRML` kept the old ones and
  the old value.
- A rebuild by `setInnerRml`, `setText`, `appendRml`, `removeChildren` or `remove` settles the select's value at once
  (the option marked `selected`, else the first), as RmlUi would at the next update, and queues no `change`.

### Tests

- 162 checks. New: append and removal (with handles, listeners, a binding and the scroll offset kept), a select's
  options, the clipboard, key and click modifiers, focus and blur, double clicks against the clock, a drag and drop
  by injected pointer input (and a dragged element removed before the poll), and a tooltip's parts.

## [0.1.1] — 2026-10-14

Additions and fixes only: no existing signature changes.

- `Element` scrolls: `scrollTop` / `setScrollTop(top:)`, `scrollLeft` / `setScrollLeft(left:)`, `scrollHeight`,
  `scrollWidth`, `clientHeight` and `clientWidth`, RmlUi's own accessors under the DOM's names. The sizes are the last
  layout's, valid after the context's `update()`, and a set clamps against them.
- `Element.setAttributeQuietly(name:, value:)` and `Element.removeAttributeQuietly(name:)`: attribute writes that queue
  no `change`, as `setValue(quiet: true)` does for values. Writing a checkbox's `checked` no longer has to raise one.
- A range's `change` is queued only when its value changed. RmlUi raises one on every slider movement, including a
  step past either end and a drag within one step; the shim drops those whose value equals the range's `value`
  attribute (the value before the movement). Real changes, one per step crossed, are all kept.
- Docs and comments name no particular program: the provenance notes say "two games" and keep the facts.
- `tests/`: 116 checks (scroll values after an update, `change` counts for quiet and loud attribute writes and for
  range movements).

## [0.1.0] — 2026-10-13

The first version, made general from the UI shim two games had built for their menus. README.md, "Where it came from", lists what changed and why.

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
