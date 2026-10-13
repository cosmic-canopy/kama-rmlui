# @kama/rmlui

[RmlUi](https://github.com/mikke89/RmlUi) 6.3 for [kama](https://kama-lang.org): HTML/CSS-like documents (RML and
RCSS), laid out and styled by RmlUi, driven from kama. A general binding with no opinion about the program using it.
It provides contexts, documents from a file or from text, fonts, provided images, injected input, elements found by id
or selector and then read or written, and an event queue that kama polls.

**RmlUi draws nothing.** Its render interface here *records*. `Context.render()` fills a list of draw commands
(vertices, indices, a texture, a translation, a scissor rectangle, a transform, clip-mask operations) that the
program's own renderer draws. There is no OpenGL, Vulkan or WebGPU backend inside, so it works with any renderer and
also headless.

> **Status: new, not yet published.** Native only (`@compileFor(!ARCH_WASM32)`). In a browser,
> [`@kama/ui`](../kama-ui) puts the same documents into the DOM instead. Tested on Linux x86_64. The macOS and Windows
> build switches come from the two games this was taken from, and have not been run here. Needs **kama ≥ 0.9.577**.

## Setting it up

RmlUi and FreeType are fetched pinned by SHA and built once by CMake into static archives. Your build links them.

```sh
tools/fetch-rmlui.sh        # RmlUi 6.3   @ ba95ffe8bfb6 -> third_party/rmlui
tools/fetch-freetype.sh     # FreeType 2.14.3 @ 0a0221a1347e -> third_party/freetype
make -C native              # -> native/build/<linux|darwin|windows>/lib/{librmlui,libfreetype}.a  (~12 s on 16 threads)
```

In the consumer's manifest:

- Add the dependency.
- Point the **root** project at the archives. The package's `link` already names `rmlui` and `freetype`. A package
  cannot name a library *path* (kama refuses a relative `-L` from a dependency, by design), so the root names it.

```json
"dependencies": { "@kama/rmlui": { "path": "../kama-rmlui" } },
"select": { "TARGET": {
  "LINUX":   { "ldflags": ["-L../kama-rmlui/native/build/linux/lib"] },
  "MACOS":   { "ldflags": ["-L../kama-rmlui/native/build/darwin/lib"] },
  "WINDOWS": { "ldflags": ["-L../kama-rmlui/native/build/windows/lib",
                           "-static-libgcc", "-Wl,-Bstatic", "-lstdc++", "-lpthread", "-Wl,-Bdynamic"] }
} }
```

How the paths resolve:

- They are relative to the directory `kama build` runs in.
- A `HOST` build (no `--target`) matches only a `HOST` arm, not the arm for its OS. Either list every OS's directory
  under `HOST`, as `tests/kama.json` does, or build with `--target LINUX` and so on.

The shim (`csrc/kama_rmlui.cpp`) is a `csources` entry. kama compiles it into your program, so the link uses the C++
driver and brings the C++ runtime on every platform.

⚠️ **Name your wasm target `WASM`** if a program that depends on this package also builds for the web, as `@kama/ui`'s
programs do. The sources and include tree are gated on `!ARCH_WASM32`, but kama has no such gate for `link`. The
package therefore empties its `link` in a `select.TARGET.WASM` arm, and that arm matches only a target with that
name. A consumer's own target called, say, `WEB` with a wasm triple would still pass `-lrmlui` to `wasm-ld`. The
gated-out include tree must still exist on disk (kama checks a gated entry's path), so fetch RmlUi even for a web-only
build.

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
| `Element` | Reading: `tag`, `id`, `attribute`, `hasClass`, `property` (computed), `value` (form controls), `innerRml`, `text`, `rect(area:)`, `isVisible`, `parent` / `childCount` / `child`, `byId` / `query`. Writing: `setInnerRml`, `setText` (escaped), `setAttribute` / `removeAttribute`, `setClass`, `setProperty` / `removeProperty` (inline style), `setValue(value:, quiet:)`. Actions: `focus`, `blur`, `click`, `scrollIntoView`, `listen(event:, flags:)`. |
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
- **Provided images.**
  - The shim opens no image files. You decode the image and hand over premultiplied RGBA8 under the name RmlUi
    resolves the `src` to: a leading `/` is stripped, and any other path is joined onto the document's directory.
  - **Provide before loading the document that shows it.** RmlUi records a miss permanently.
- **Textures.** A handle is a slot that gets recycled. Key a GPU upload on `Texture.generation`, never on the handle,
  the size or the pointer. (The Mallow Man shipped garbled text from that once.)
- **Fonts.** A codepoint no loaded face has draws as *nothing*. Load a broad face last, with `fallback: true`.
- **RmlUi's log** is queued (`pollLog`) and echoed to stdout by default. Otherwise "the document loaded but nothing
  laid out" is silent.
- **Every form control is in RmlUi's tab ring.** RmlUi gives each one an *inline* `tab-index: auto`, which no
  stylesheet rule overrides. To take one out, write `style="tab-index: none"` on the element.

## What is vendored, and why

| | version | pin | licence |
|---|---|---|---|
| RmlUi (`rmlui_core` only: no debugger, no Lua, no samples) | 6.3 | `ba95ffe8bfb6370efb2cdcca927eaad4710c5413` | MIT |
| robin_hood, itlib (inside RmlUi's `Include/RmlUi/Core/Containers/`) | as in RmlUi 6.3 | same tree | MIT, separate notices |
| FreeType | 2.14.3 | `0a0221a1347e2f1e07c395263540026e9a0aa7c7` | FTL (elected; dual FTL / GPLv2) |

Fetched by SHA, not committed: about 10 MB of C++, and the SHA is the real pin. The tag is checked only as a warning.
`third_party/` and `native/build/` are gitignored.

**CMake rather than `csources`.**

- @kama/sodium compiles libsodium's 119 C files into every consumer's build. RmlUi is about 200 C++ files, and
  FreeType's build is driven by its CMake configuration.
- As `csources`, a debug `kama build` would link an `-O0` layout engine, and every release build would recompile it.
- `native/Makefile` builds both once at `-O2`, whatever the consumer's tier.
- That is kama's documented arrangement for a CMake library (docs/targets.md, "A package carries sources, not
  binaries"): the package says what it links, and the root says where it is.

The switches, carried over with their reasons from both games' `native/Makefile`:

- **FreeType first, minimal and static.** HarfBuzz, Brotli, BZip2, PNG and system zlib are all OFF.
  friendly-fire-department measured MSYS2's FreeType pulling 14 more DLLs, some of them LGPL, into an .exe.
- **RmlUi finds that FreeType through `CMAKE_PREFIX_PATH`.** ⚠️ An RmlUi configured *before* FreeType was built caches
  the platform's FreeType: `rm -rf native/build/<plat>/rmlui` once. Check `FREETYPE_LIBRARY_RELEASE` in its
  `CMakeCache.txt`.
- **`RMLUI_STATIC_LIB`.** Without it, Windows treats every call as `dllimport` and the link fails on `__imp__ZN3Rml…`.
  The shim defines it itself before including RmlUi, because a dependency's `cflags` would reach every translation
  unit of the consumer.
- **C++17.** kama compiles C++ `csources` as `gnu++17`.
- **On Windows the compilers are GNU** (`g++`). C++ compiled by clang++ against GCC's static libstdc++ printed 35–100
  COMDAT warnings a link. Build kama programs with `--cxx g++` there too, so the shim matches. (Untested here.)
- **`-ffile-prefix-map`**, so a shipped binary does not name the build machine's home directory. `-fPIC` everywhere
  except Windows.
- **A compiler stamp** (`native/build/<plat>/.toolchain`). A new compiler rebuilds both archives from clean; make
  cannot see a compiler change on its own.

## What a program that ships this must carry

`licenses/manifest.tsv` lists the texts. All three ship in **every native build** that links this package:

1. **RmlUi's MIT notice**: `licenses/RmlUi-MIT.txt`.
2. **robin_hood's and itlib's MIT notices**: `licenses/RmlUi-containers-MIT.txt`. This is a *separate* notice from
   separate copyright holders. friendly-fire-department's FR-28 audit found 472 of their symbols in a shipped binary
   whose notice named neither.
3. **FreeType's FTL**: `licenses/FreeType-FTL.txt`. The FTL also asks for a **credit line in the documentation** (a
   credits screen counts):

   > Portions of this software are copyright © 2026 The FreeType Project (www.freetype.org). All rights reserved.

   The FreeType built here also contains its own copy of zlib's inflate (`src/gzip/`, zlib licence), which asks
   nothing of a binary distribution.

The fonts the tests use (RmlUi's sample Lato, SIL OFL 1.1) come with the fetch and are never part of a program.

## Tests

```sh
tools/fetch-rmlui.sh && tools/fetch-freetype.sh && make -C native
KAMA=/path/to/kama tools/test.sh      # tests/ debug and release
```

`tests/` is one headless program, **91 checks**, passing in debug and in release. It is also clean under
AddressSanitizer with leak checking (run by hand: `cflags`/`ldflags` `-fsanitize=address` in a `tests/kama.local.json`).
It covers:

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
- Text, markup, attributes, classes and inline properties read back after writing.
- **A child handle going dead, not dangling, after a rewrite.** A binding keeping its handle across that rewrite and
  carrying its listener to the new element.
- Two contexts recording separately.
- A closed document, a destroyed context and a shut-down library leaving every handle dead and every call harmless.

There is no wasm leg: this package is native by construction. `@kama/ui`'s web build depends on it, and that build is
what shows the gates hold. The shim's `#error` tripwire never fires there.

## Where it came from

The shim is the-mallow-man's `ui/src/tmm_ui_native.cpp` and `tmm_ui.h` (E7b/E7c, 2026-10-07). Those were
friendly-fire-department's `ffd_ui_native.cpp` and `ffd_ui.h` (a676ca3, last changed 2026-09-29), renamed and extended
by the-mallow-man for text entry, focus, a real clock and inline properties. **The Mallow Man's copy was the base**.
After the rename, friendly-fire-department's copy held nothing the newer one lacked; the only lines unique to it were
a comment giving its stage size.

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
| built into `libtmm_ui.a` by the game's Makefile | a `csources` entry | the package compiles its own glue |

## Not here yet

- RmlUi's layers, filters and shaders: `box-shadow`, `filter`, gradients. RmlUi skips what the interface does not
  implement, so those decorations do not draw.
- Data bindings (RmlUi's data models), custom elements and decorators.
- A file interface: documents and fonts are read from disk by path, or passed as text and bytes.
- The debugger and Lua plugins.
- A wasm build of RmlUi.

## License

`@kama/rmlui` is licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE))
- MIT license ([LICENSE-MIT](LICENSE-MIT))

at your option. RmlUi (and the containers it vendors) is MIT and FreeType is FTL. Their texts are in `licenses/`, and
the section above says what a program that ships them carries.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in `@kama/rmlui` by you,
as defined in the Apache-2.0 license, shall be dual licensed as above, without any additional terms or conditions.
