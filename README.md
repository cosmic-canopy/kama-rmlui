# @kama/rmlui

[RmlUi](https://github.com/mikke89/RmlUi) 6.3 for [kama](https://kama-lang.org): HTML/CSS-like documents (RML and
RCSS), laid out and styled by RmlUi, driven from kama. A general binding with no opinion about the program using it.
It provides contexts, documents from a file or from text, fonts, provided images, injected input, elements found by id
or selector and then read or written, and an event queue that kama polls.

**RmlUi draws nothing.** Its render interface here *records*. `Context.render()` fills a list of draw commands
(vertices, indices, a texture, a translation, a scissor rectangle, a transform, clip-mask operations) that the
program's own renderer draws. There is no OpenGL, Vulkan or WebGPU backend inside, so it works with any renderer and
also headless.

> **Status: published** (registry.kama-lang.org, 0.1.0 on 2026-10-13). Native only (`@compileFor(!ARCH_WASM32)`). In a browser,
> [`@kama/ui`](../kama-ui) puts the same documents into the DOM instead. Tested on Linux x86_64 (debug, release,
> AddressSanitizer). macOS and Windows are not yet run here. Needs **kama ≥ 0.9.577**.

## Setting it up

RmlUi is vendored (`third_party/rmlui`) and compiled by kama into your program. FreeType, its font engine, comes from
[`@kama/freetype`](https://github.com/cosmic-canopy/kama-freetype) on the registry, which is also vendored. There is nothing to fetch, nothing to build with CMake,
and no `-L` path to set.

```json
"dependencies": { "@kama/rmlui": { "version": "^0.1.1" } }
```

To build against a checkout instead (working on the package itself), override it in your gitignored
`kama.local.json` rather than editing `kama.json`:

```json
{ "overrides": { "@kama/rmlui": { "path": "../kama-rmlui" } } }
```

What that costs and buys:

- **It is about 190 C++ files.** The first debug build compiles them in about 7 s on 16 threads. After that,
  `.kama-cache/` reuses them (half a second). A `--release` build compiles them again each time, about 9 s.
- **A debug build compiles RmlUi at `-O0`.** To keep the layout engine at release speed while debugging your own code,
  add this to the **root** manifest. Only `-O` changes; `-g` stays.

  ```json
  "debug": { "optimize": ["rmlui", "freetype"] }
  ```

- **A debug build turns on RmlUi's own assertions**, because `RMLUI_DEBUG` follows `NDEBUG`. The CMake archives
  were always release builds, so this is new. One example: a program that ends without `RmlUi.shutdown()` now prints
  `rmlui assert: RenderInterface is being destroyed …` at exit.
- **Every source is gated `!ARCH_WASM32`.** That covers the shim, the RmlUi sources and the include tree. A wasm
  build (`@kama/ui`'s web leg) compiles none of it, whatever the consumer calls its target. The package links no
  library by name, so there is no `-l` that could reach `wasm-ld`.
- **Windows** (untested here): link libstdc++ statically, as the two games did. Build with `--cxx g++` so the C++ all
  comes from one compiler.

  ```json
  "select": { "TARGET": { "WINDOWS": { "ldflags": ["-static-libgcc", "-Wl,-Bstatic", "-lstdc++", "-lpthread", "-Wl,-Bdynamic"] } } }
  ```

## Using it

```kama
import { rmlui::RmlUi, rmlui::RmlUiError, rmlui::Context, rmlui::Document, rmlui::Element, rmlui::Event,
         rmlui::Modal, rmlui::Focus, rmlui::BoxArea, rmlui::Command, rmlui::LISTEN_BUBBLE, rmlui::MOD_NONE };

fn void demo() {
    Result<RmlUi, RmlUiError> started = RmlUi.start();
    RmlUi ui = match (started) { case Ok(value: u): u; case Err(error: e): { return; } };
    ui.loadFontFace(path: "assets/fonts/Inter-Regular.ttf", fallback: true);
    Result<Context, RmlUiError> made = Context.create(ui: ui, name: "menu", width: 1920, height: 1080);
    Context ctx = match (made) { case Ok(value: c): c; case Err(error: e): { return; } };

    Optional<Document> loaded = ctx.loadDocument(path: "assets/ui/menu.rml");
    Document doc = match (loaded) { case Some(value: d): d; case None: { return; } };
    doc.show(modal: Modal::None, focus: Focus::Auto);          // focuses the `autofocus` element
    Optional<Element> play = doc.byId(id: "play");
    match (play) { case Some(value: p): { p.listen(event: "click", flags: LISTEN_BUBBLE); } case None: { } };

    // each frame: input in, update, render, draw, then drain the events
    ctx.mouseMove(x: 960.0, y: 540.0, mods: MOD_NONE);
    ctx.update();
    ctx.render();
    int32 i = 0;
    while (i < ctx.commandCount()) {
        Command c = ctx.command(index: i);    // c.kind, c.vertices (8 floats a vertex), c.indices, c.texture, …
        i = i + 1;
    }
    bool more = true;
    while (more) {
        Optional<Event> e = ctx.pollEvent();
        match (e) { case None: { more = false; } case Some(value: ev): { /* ev.type, ev.element, ev.target */ } };
    }
    ctx.destroy();
    ui.shutdown();
}
```

| type | what |
|---|---|
| `RmlUi` | The library: `start` / `shutdown` (reference counted). Also `loadFontFace`, `loadFontFaceBytes`, `provideImage`, `setTime`, `pollLog` / `setLogEcho`, `textureCount` / `texture`, and the last event's `eventName` / `eventInt` / `eventFloat` / `eventString`. |
| `Context` | One surface in framebuffer pixels: `create` / `destroy`, `setDimensions`, `setDpRatio`, `update`, `render`, `commandCount` / `command`. Input: `mouseMove`, `mouseButton`, `mouseWheel`, `mouseLeave`, `key`, `text`, `textUtf8`; each returns **true when the UI consumed it**. Also `loadDocument` / `loadDocumentRml`, `focusElement` / `hoverElement` / `elementAtPoint`, `pollEvent`, `clearEvents`. |
| `Document` | `show(modal:, focus:)`, `hide`, `close`, `title`, `byId`, `query`, `focused` (None when nothing in it is), `nextTab`, `bind`, `bindingOf`, `root`. |
| `Element` | Reading: `tag`, `id`, `attribute`, `hasClass`, `property` (computed), `value` (form controls), `innerRml`, `text`, `rect(area:)`, `isVisible`, `parent` / `childCount` / `child`, `byId` / `query`. Writing: `setInnerRml`, `setText` (escaped), `setAttribute` / `removeAttribute`, `setAttributeQuietly` / `removeAttributeQuietly`, `setClass`, `setProperty` / `removeProperty` (inline style), `setValue(value:, quiet:)`. Scrolling: `scrollTop` / `setScrollTop(top:)`, `scrollLeft` / `setScrollLeft(left:)`, `scrollHeight`, `scrollWidth`, `clientHeight`, `clientWidth`. Actions: `focus`, `blur`, `click`, `scrollIntoView`, `listen(event:, flags:)`. |
| `Binding` | An id held across rewrites: `element`, `listen`, `id` (see below). |
| `Event` | `listener`, `binding`, `type` (`EventType`), `element` (where it was heard), `target` (where it happened). |
| `Command`, `Texture`, `Vertex`, `Rect` | The recording: `kind` (`Draw`, `ClipEnable`, `ClipDisable`, `ClipSet`, `ClipSetInverse`, `ClipIntersect`), raw pointers for an upload plus bounds-checked `vertex(i)` / `index(i)` / `byteAt(i)`. |
| `KEY_*`, `MOD_*` | RmlUi's own key identifiers and modifier bits. The shim `static_assert`s every one. |

## The rules it keeps

- **Everything is a value over an integer handle, and every handle is safe to hold.**
  - RmlUi owns the objects on the C++ side. Lifetimes are explicit: `shutdown`, `destroy`, `close`. That is the trade
    a window or an audio device makes, and it lets a program keep its UI in a module `static`.
  - Each element handle tracks its element with RmlUi's own `ObserverPtr`, and each handle carries a generation.
  - So a handle whose object is gone (a rewrite, a closed document, a destroyed context, a shut-down library) is
    **dead**: getters answer None, "" or 0, and setters do nothing. It never reads freed memory.
  - Both games' shims held a raw `Element*` per binding. They documented the use-after-free that comes from that, and
    repaired it with a write counter, because a replacement element often reuses the old one's address. An observer
    pointer does not have that problem.
- **Events are polled, never pushed.** A listener lives in C++ and appends to its **context's** queue. kama drains the
  queue with `Context.pollEvent()`. No callback crosses the seam.
  - Events **bubble**: a listener on a container hears its children too, and `target` says where the event happened.
  - Stopping propagation is the `LISTEN_STOP` flag, because it has to be decided inside RmlUi's dispatch.
  - A listener goes with its element: RmlUi detaches it (`OnDetach`). That retires the games' rule that listeners had
    to be detached before a document closed.
- **Bindings** (`Document.bind(id:)`) give the same handle for the same id while the document lives.
  - After a `setInnerRml` destroys the element, the binding finds the new one by id and moves its listeners to it.
    That happens the next time the binding is used: "re-bind what you wrote, after the write".
  - Bindings exist because a UI that rewrites a list still wants to talk about "row 3", and because a kama value
    cannot hold the id table itself.
- **Strings out** come back through one scratch buffer and are copied into a kama `string`. **Strings in** are copied
  before the call returns.
- **`setInnerRml` is a parser.** Text another person typed goes through `setText` or `encodeRml` (RmlUi's own
  `EncodeRml`).
- **`setValue(quiet: true)`**: RmlUi raises `change` when a script writes a range or a checkbox. A browser does not.
  `quiet` drops that event, so only a person's edit is queued.
- **`setAttributeQuietly` / `removeAttributeQuietly`** are the same for attributes. Writing a checkbox's `checked`
  (or a radio's, or a range's `value`) makes RmlUi raise `change`; the quiet writes queue none. Mirror a program's
  state into its controls with these.
- **A range's `change` is queued only when its value changed.** RmlUi's slider raises `change` on every movement, so
  a step past either end, or a drag that stays within one step, raises one with the value it already had. The shim
  drops those: it compares the event's value with the range's `value` attribute, which RmlUi updates only after a real
  change. Every real change is kept, one per step crossed, so a slider still previews live. A loud script write that
  moves the value is a real change and is kept.
- **Scrolling reads the last layout.** `scrollHeight`, `clientHeight` and the rest are valid after the context's
  `update()`, and `setScrollTop` / `setScrollLeft` clamp against them (to `0..scrollHeight - clientHeight`, whole
  pixels). To keep a log on its newest line: write the line, `update()`, then `setScrollTop(top: log.scrollHeight())`.
  A change of offset raises `scroll`.
- **Provided images.**
  - The shim opens no image files. You decode the image and hand over premultiplied RGBA8 under the name RmlUi
    resolves the `src` to: a leading `/` is stripped, and any other path is joined onto the document's directory.
  - **Provide before loading the document that shows it.** RmlUi records a miss permanently.
- **Textures.** A handle is a slot that gets recycled. Key a GPU upload on `Texture.generation`, never on the handle,
  the size or the pointer. (A game shipped garbled text from that once.)
- **Fonts.** A codepoint no loaded face has draws as *nothing*. Load a broad face last, with `fallback: true`.
- **RmlUi's log** is queued (`pollLog`) and echoed to stdout by default. Otherwise "the document loaded but nothing
  laid out" is silent.
- **Every form control is in RmlUi's tab ring.** RmlUi gives each one an *inline* `tab-index: auto`, which no
  stylesheet rule overrides. To take one out, write `style="tab-index: none"` on the element.

## What is vendored, and why

| | version | pin | licence |
|---|---|---|---|
| RmlUi: `Source/Core` and `Include` (Core and Config) only | 6.3 | `ba95ffe8bfb6370efb2cdcca927eaad4710c5413` | MIT |
| robin_hood and itlib (inside RmlUi's `Include/RmlUi/Core/Containers/`) | as in RmlUi 6.3 | same tree | MIT, separate notices |
| FreeType, through the `@kama/freetype` dependency | 2.14.3 | that package's `VENDORED` | FTL (taken over GPLv2) |

`tools/vendor-rmlui.sh` is the only thing that writes `third_party/rmlui/`. It works like this:

- It downloads GitHub's archive of the pinned commit and checks its SHA-256.
- It copies `LICENSE.txt`, `Include/RmlUi/Core.h`, `Include/RmlUi/{Core,Config}/` and `Source/Core/` byte for byte.
  Only `.cpp`, `.h`, `.inl` and `.hpp` files are copied, plus the containers' `LICENSE.txt`.
- It regenerates the `csources` block of `kama.json`.
- It records the result in `third_party/rmlui/VENDORED`.

Samples, Backends, Tests, Utilities, Dependencies and the build files are **not** vendored, and neither are the
Debugger, Lua, SVG and Lottie plugins.

**The licence gate: MIT only.** RmlUi 6 keeps its licence in `LICENSE.txt`, not in each file. So:

- A kept file that names no licence is covered by `LICENSE.txt`.
- A file that names one must name MIT.
- Any other licence, any copyleft text, or a `LICENSE.txt` that is no longer MIT fails the run.

At 6.3 the gate **checks 442 files and keeps 442**. Three of them name MIT themselves: robin_hood and the two itlib
headers. It refuses none.

**How it compiles without leaking into your build.** RmlUi's CMake gives `rmlui_core` three definitions:
`RMLUI_VERSION`, `RMLUI_FONT_ENGINE_FREETYPE`, and, for a static library, `RMLUI_STATIC_LIB`. A `cflags` entry would
reach every translation unit of every consumer. Instead:

- Each RmlUi source is compiled through a generated wrapper in `csrc/rmlui/`. The wrapper includes
  `csrc/rmlui/kama_rmlui_build.h` and then the pristine `.cpp`.
- RmlUi's own `Source/` directories are never on the include path, so its `Pool.h`, `Clock.h` or `Template.h` cannot
  shadow one of yours. Your code sees `csrc/` (the shim's header) and `third_party/rmlui/Include`.

The switches carried over from both games' builds, with their reasons:

- **`RMLUI_STATIC_LIB`**, defined by the shim and the wrappers before any RmlUi header. Without it, Windows treats
  every call as `dllimport` and the link fails on `__imp__ZN3Rml…`.
- **C++17**, because RmlUi 6 needs it. kama compiles C++ `csources` as `gnu++17`.
- **FreeType minimal**: no HarfBuzz, Brotli, BZip2, PNG or zlib. One of the games measured MSYS2's FreeType
  pulling 14 more DLLs, some of them LGPL, into an .exe. That is now `@kama/freetype`'s configuration.

## What a program that ships this must carry

`licenses/manifest.tsv` lists the texts. All of them ship in **every native build** that links this package:

1. **RmlUi's MIT notice**: `third_party/rmlui/LICENSE.txt`.
2. **robin_hood's and itlib's MIT notices**: `third_party/rmlui/Include/RmlUi/Core/Containers/LICENSE.txt`. This is a
   *separate* notice from separate copyright holders. A licence audit found 472 of their symbols in a
   shipped game's binary whose notice named neither.
3. **FreeType's**, from `@kama/freetype` (its README and `licenses/manifest.tsv`):
   - the FTL, with its credit line in the documentation (a credits screen counts):

     > Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.

   - the MIT notice of FreeType's hashing code.

The test font (Lato, SIL OFL 1.1, `tests/fonts/`) is never part of a program, and it is not published.

## Tests

```sh
KAMA=/path/to/kama tools/test.sh      # tests/: debug, release, and debug under AddressSanitizer (Linux)
```

`tests/` is one headless program, **116 checks**. It passes in debug, in release, and under AddressSanitizer with leak
checking. The ASan leg wraps `cc`/`c++` with `-fsanitize=address`, so RmlUi, FreeType and the shim are all
instrumented. Its font is Lato, committed under `tests/fonts/` with its OFL text. It covers:

- Version 6.3.
- Font and log handling, including a missing face showing up in the log queue.
- A provided image laid out at its own size.
- A second context with the same name refused.
- A document from text and one from a file.
- The recorded commands: geometry, indices in range, textured text, a transform from `rotate()`, and a clip mask from
  `overflow: hidden` inside a rounded box.
- Inked pixels in the glyph atlas.
- Elements found by id and by selector, with their boxes.
- **An injected mouse click arriving as a polled event**, bubbling to its container with the right target. A removed
  listener stays silent, and `LISTEN_STOP` stops the bubble.
- `Element.click`.
- TAB, the arrows and RETURN.
- Typing, Backspace, Home and Delete in a text field, and a keydown's `key_identifier`.
- Quiet and loud value writes.
- Quiet and loud attribute writes, counted in `change` events: a checkbox's `checked` set and removed both ways, and
  a range's `value`.
- A range's `change` only when its value changed, heard on the range and on its container: RIGHT and LEFT one step
  each, steps past both ends, a press at the bar, a drag within one step, and a drag across steps (one change a step).
- Scrolling: a log's sizes after the update and still stale before it, a clamped `setScrollTop`, a log kept on its
  newest line, the same across with `setScrollLeft`, and a dead element reading 0.
- Text, markup, attributes, classes and inline properties read back after writing.
- **A child handle going dead, not dangling, after a rewrite.** A binding keeping its handle across that rewrite and
  carrying its listener to the new element.
- Two contexts recording separately.
- A closed document, a destroyed context and a shut-down library leaving every handle dead and every call harmless.

There is no wasm leg: this package is native by construction. `@kama/ui`'s web build depends on it, and that build is
what shows the gates hold. The shim's `#error` tripwire never fires there.

## Where it came from

The shim began as the UI shim two games had built for their RmlUi menus. The second game copied the first's, renamed
it, and extended it for text entry, focus, a real clock and inline properties. **The newer copy was the base**: the
older one held nothing it lacked, and the only lines unique to it were a comment giving its stage size.

What changed on the way from a game's shim to a general binding:

| the games' shim | here | why |
|---|---|---|
| one file-static context, one document | contexts are handles, any number of documents | general |
| `bind(id)`: the only way to reach an element, repaired after rewrites by a write counter | element handles for any element, tracked by `ObserverPtr`; bindings re-find their element by id | no stale pointer can be read; works for elements found by selector or tree walk |
| listeners attached by bind only, detached by hand before a close | listeners on any element and any event, retired by RmlUi's `OnDetach` | removes the close-ordering use-after-free rule |
| one global event queue: click and change only | a queue per context, any event, its target, its parameters | two UIs in one program do not see each other's events |
| keys: GLFW codes, a fixed subset | RmlUi's `KeyIdentifier`s and modifiers | the subset and the translation are a seam's policy (`@kama/ui` keeps them) |
| draws and scissors recorded | also transforms and clip masks | RCSS `transform` and rounded `overflow: hidden` now reach the renderer |
| `uint8_t*` / `float*` out | `const` pointers, `UnsafeConstPtr` in kama | the games' headers said this was owed once kama could spell it |
| `printf` of RmlUi's log | a queue plus stdout | tests can assert on it |
| built into a static library by the game's Makefile | a `csources` entry | the package compiles its own glue |

## Not here yet

- RmlUi's layers, filters and shaders: `box-shadow`, `filter`, gradients. RmlUi skips what the interface does not
  implement, so those decorations do not draw.
- Data bindings (RmlUi's data models), custom elements and decorators.
- A file interface: documents and fonts are read from disk by path, or passed as text and bytes.
- The debugger and Lua plugins.
- A wasm build of RmlUi.

## Publishing

`kama publish` ships exactly what git tracks, minus `publish.exclude` (`tools/`, `tests/`, the agent notes). The three
packages publish in order: **@kama/freetype**, then **@kama/rmlui**, then **@kama/ui**. A published package cannot
carry a `path` dependency, so just before this one publishes, change its `@kama/freetype` dependency from the path to
`{ "version": "^0.1.0" }`, once `@kama/freetype` 0.1.0 is on the registry.

## License

`@kama/rmlui` is licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE))
- MIT license ([LICENSE-MIT](LICENSE-MIT))

at your option. RmlUi (and the containers it vendors), in `third_party/rmlui`, is MIT. FreeType comes from
`@kama/freetype` under the FTL. "What a program that ships this must carry" lists the notices.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in `@kama/rmlui` by you,
as defined in the Apache-2.0 license, shall be dual licensed as above, without any additional terms or conditions.
