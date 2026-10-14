// kama_rmlui.h — the C ABI between kama and RmlUi 6.3 (csrc/kama_rmlui.cpp is the only thing that includes RmlUi).
//
// From the-mallow-man's ui/src/tmm_ui.h (E7b/E7c, 2026-10-07), itself from friendly-fire-department@a676ca3's
// ffd_ui.h, made GENERAL: several contexts, any number of documents, element handles for any element (not only ones
// bound by id), listeners on any event, and the optional RenderInterface features (transforms, clip masks). The
// one-context, one-document, bind-by-id policy those games had now lives in their UI seam (@kama/ui), on top of this.
//
// THE SHAPE, AND WHY
// ------------------
// RmlUi is C++ with no C API, so this header is the whole boundary: kama sees C scalars and pointers, never a C++
// type, and NEVER A CALLBACK.
//
// RmlUi RENDERS NOTHING ITSELF. It hands its render interface geometry — vertices, indices, a translation, a texture,
// a scissor rectangle, a transform, clip-mask operations — and expects the application to draw it. The shim's render
// interface RECORDS instead of drawing, per context, and the `kama_rmlui_command_*` accessors read that recording
// back. So RmlUi fits any renderer the consumer already has (a 2D batcher, a WebGPU pass) rather than needing one.
//
//   Rml::Context::Render()  --calls-->  RecordingRenderer  --appends-->  [ the context's command list ]
//                                                                              |
//                                       kama_rmlui_command_*(ctx, i)  <--reads--+
//
// EVENTS COME OUT BY POLLING. RmlUi's model is "register a listener, get called back"; here the listener lives in
// C++ and pushes into a queue kama drains with `kama_rmlui_poll_event`. A toolkit calling into kama from inside its
// own layout pass is reentrancy nobody wants (the platform-seam rule: "kama polls, so there is no reentrancy and no
// lifetime question at the seam"), so this stays even now that kama can store and call an fnptr.
//
// EVERYTHING IS AN INTEGER HANDLE, AND NO HANDLE IS EVER STALE IN THE DANGEROUS WAY
// ---------------------------------------------------------------------------------
// Contexts, elements (a document is an element) and listeners are handles >= 1; 0 is "none". An element handle
// tracks its element with RmlUi's own `ObserverPtr`, so a handle whose element was destroyed — by `set_inner_rml`,
// by closing its document — answers as DEAD (every getter returns its "none" value, every setter does nothing)
// instead of reading freed memory. ⚠️ **That is the fix for the hazard both games documented at length**: their
// shims held a raw `Rml::Element*` per binding, a rewrite freed it, and the-mallow-man repaired it with a write
// counter because "a destroyed element's memory is commonly reused by the element that replaces it, so the two
// addresses are equal and the comparison lies". An observer pointer cannot lie that way. A handle also carries a
// generation, so a recycled slot never answers to an old handle.
//
// The same element always answers with the same handle while it lives (looking it up twice gives one handle), so
// dispatch on the kama side is an integer compare.
//
// STRINGS OUT: a getter that produces text returns its LENGTH IN BYTES (or -1 for "none") and leaves the UTF-8 bytes
// in a scratch buffer that `kama_rmlui_string_data()` points at, valid until the next string-producing call. One
// computation, one copy into a kama `string`, and no buffer sizing on the kama side. Strings IN are NUL-terminated
// UTF-8 and copied before the call returns.
//
// THREADS: none. RmlUi is single-threaded; call all of this from one thread.
//
// ONE CALLER: src/rmlui.kama is the only kama file that `extern`s this header.

#ifndef KAMA_RMLUI_H
#define KAMA_RMLUI_H

#ifdef __EMSCRIPTEN__
// A tripwire, not a portability guard. This header's kama side is `file @compileFor(!ARCH_WASM32)`, and the shim's and
// every RmlUi wrapper's `csources` entry carries the same gate, because RmlUi is the native leg (a browser build uses
// the DOM). If either gate ever stops holding, fail here rather than at a confusing link error about a C++ symbol.
#error "kama_rmlui.h reached a wasm build — the @compileFor(!ARCH_WASM32) gate did not hold"
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- the library -----------------------------------------------------------------------------------------------

// Start RmlUi (the system and render interfaces, then Rml::Initialise). Reference counted: every successful call
// must be matched by one `kama_rmlui_shutdown`, and the last one shuts RmlUi down — destroying every context,
// document, element, binding and listener (every handle into them then reads as dead). Returns 1 on success, 0 if
// RmlUi would not initialise. A shutdown with nothing started does nothing.
int32_t kama_rmlui_initialise(void);
void    kama_rmlui_shutdown(void);
int32_t kama_rmlui_is_initialised(void);

// RmlUi's version string ("6.3"), into the scratch buffer. Its length.
int32_t kama_rmlui_version(void);

// The scratch buffer every string getter fills (see "STRINGS OUT").
const uint8_t* kama_rmlui_string_data(void);

// RmlUi's clock, in seconds. Animations, transitions and the caret's blink read it. MONOTONIC: a value earlier than
// the current one is ignored, so a clock that steps backwards holds rather than rewinding animations. Nothing
// advances it on its own — a headless run that never sets it is deterministic.
void   kama_rmlui_set_time(double seconds);
double kama_rmlui_time(void);

// ---- fonts -------------------------------------------------------------------------------------------------------

// Load a font face from a file (TTF/OTF). `fallback` marks it as a LAST RESORT for codepoints no other face has.
// ⚠️ NOT COSMETIC: RmlUi draws a codepoint no loaded face carries as NOTHING AT ALL — not a box, not a blank (the
// games' display face lacked five of their glyphs, and only a fallback face made them appear). Load the display
// faces first and the fallback last. 1 on success; a failure is also logged.
int32_t kama_rmlui_load_font_face(const char* path, int32_t fallback);

// The same from bytes in memory (a font embedded in the program). The bytes are COPIED and kept until shutdown.
// `family` empty means "read it from the face"; `weight` 0 is automatic, else 100..900; `italic` 1 for an italic.
int32_t kama_rmlui_load_font_face_memory(const uint8_t* data, int32_t size, const char* family, int32_t weight,
                                         int32_t italic, int32_t fallback);

// ---- provided images ---------------------------------------------------------------------------------------------
//
// THIS SHIM OPENS NO IMAGE FILES. The caller decodes a picture and hands over its pixels under the NAME RmlUi will
// ask for; RmlUi's `LoadTexture` then matches the name. RGBA8, PREMULTIPLIED alpha, tightly packed,
// `width * height * 4` bytes, COPIED (the caller's buffer may die at once). Providing a name again replaces the pixels.
//
// ⚠️ `name` IS THE RESOLVED PATH, NOT THE `src` ATTRIBUTE. RmlUi joins an `<img src>` against the owning document's
// path first: a leading '/' is stripped and the rest used verbatim; anything else is joined onto the document's own
// directory. So a document `assets/ui/title.rml` saying `src="/assets/ui/badge.png"` asks for `assets/ui/badge.png`.
// A mismatch is not an error — it is a picture that never appears (and one log line).
//
// ⚠️⚠️ PROVIDE BEFORE THE DOCUMENT THAT SHOWS IT IS LOADED. RmlUi resolves an image's intrinsic size during layout,
// inside the load, and records a failure PERMANENTLY (its texture database never retries one), so a picture provided
// afterwards never appears for the life of the process. Measured by both games, not read off the source.
//
// A provided image costs a texture slot only once something displays it (minted on first request, memoised).
void kama_rmlui_provide_image(const char* name, const uint8_t* rgba, int32_t width, int32_t height);

// ---- RmlUi's log -----------------------------------------------------------------------------------------------------
//
// Every message RmlUi logs (a parse error in a document or a stylesheet, a missing font, an unknown element) is queued
// here — the last 512 — and, unless echo is off, printed to stdout as `rmlui <type>: <message>`. Echo is ON by
// default because "the document loaded but nothing laid out" is otherwise silent.
#define KAMA_RMLUI_LOG_ALWAYS  0
#define KAMA_RMLUI_LOG_ERROR   1
#define KAMA_RMLUI_LOG_ASSERT  2
#define KAMA_RMLUI_LOG_WARNING 3
#define KAMA_RMLUI_LOG_INFO    4
#define KAMA_RMLUI_LOG_DEBUG   5
void    kama_rmlui_set_log_echo(int32_t on);
// Pop the oldest message: its length (text in the scratch buffer) and `*type`, or -1 when none is queued.
int32_t kama_rmlui_poll_log(int32_t* type);

// ---- contexts ----------------------------------------------------------------------------------------------------------

// A context is one independent UI surface — sized in FRAMEBUFFER PIXELS, never logical points. `name` must be unique
// among live contexts (empty: one is made up). Returns its handle, or 0.
int32_t kama_rmlui_context_create(const char* name, int32_t width, int32_t height);
// Destroy it, with every document in it. A dead or unknown handle is ignored.
void    kama_rmlui_context_destroy(int32_t context);
int32_t kama_rmlui_context_is_live(int32_t context);
void    kama_rmlui_context_set_dimensions(int32_t context, int32_t width, int32_t height);
// RmlUi's `dp` unit: one dp is `ratio` framebuffer pixels. Text is rasterised at its final size, so setting this from
// the window's scale keeps it crisp rather than scaling a bitmap.
void    kama_rmlui_context_set_dp_ratio(int32_t context, float ratio);
// Layout, animation and the deferred work (a closed document is destroyed here). Once per frame, before render.
void    kama_rmlui_context_update(int32_t context);
// Run the render pass, which REPLACES this context's command list. Nothing is drawn.
void    kama_rmlui_context_render(int32_t context);

// The element holding keyboard focus in this context (often a document, when nothing in it is focused), the one
// under the pointer, and the one at a point. Handles, or 0.
int32_t kama_rmlui_context_focus_element(int32_t context);
int32_t kama_rmlui_context_hover_element(int32_t context);
int32_t kama_rmlui_context_element_at_point(int32_t context, float x, float y);

// ---- input in ----------------------------------------------------------------------------------------------------------
//
// Every one returns 1 when the UI CONSUMED the input (the pointer was over, or interacting with, an element; a key
// went somewhere) and 0 when the application should have it — RmlUi's "propagate" result, inverted to read as a
// question about the UI. Positions are framebuffer pixels. Buttons: 0 left, 1 right, 2 middle. `mods` is a
// KAMA_RMLUI_MOD_* mask.
#define KAMA_RMLUI_MOD_CTRL       1
#define KAMA_RMLUI_MOD_SHIFT      2
#define KAMA_RMLUI_MOD_ALT        4
#define KAMA_RMLUI_MOD_META       8
#define KAMA_RMLUI_MOD_CAPSLOCK   16
#define KAMA_RMLUI_MOD_NUMLOCK    32
#define KAMA_RMLUI_MOD_SCROLLLOCK 64
int32_t kama_rmlui_context_mouse_move(int32_t context, float x, float y, int32_t mods);
int32_t kama_rmlui_context_mouse_button(int32_t context, int32_t button, int32_t down, int32_t mods);
// ⚠️ RmlUi scrolls from the HOVERED element: move the pointer first, or the wheel scrolls the body or nothing.
// In notches; positive y is down (reading further into a document), positive x is right.
int32_t kama_rmlui_context_mouse_wheel(int32_t context, float dx, float dy, int32_t mods);
int32_t kama_rmlui_context_mouse_leave(int32_t context);
// `key` is RmlUi's own `Rml::Input::KeyIdentifier` (src/keys.kama spells them, and the shim static_asserts every
// one it spells). TAB walks `tab-index`, the arrows follow `nav-*`, RETURN clicks the focused element — so a key
// arrives at the poll as an ordinary click. Editing keys edit a focused text control.
int32_t kama_rmlui_context_key(int32_t context, int32_t key, int32_t down, int32_t mods);
// Typed text, already through the keyboard layout (a platform's character event). A control character (< 32, or 127)
// is dropped: BACKSPACE, RETURN and TAB are keys, and a platform that also reports them as characters must not type them.
int32_t kama_rmlui_context_text(int32_t context, uint32_t codepoint);
int32_t kama_rmlui_context_text_utf8(int32_t context, const char* text);

// ---- documents ---------------------------------------------------------------------------------------------------------
//
// A document IS an element: the handle these return works with every `kama_rmlui_element_*` call, and every
// `kama_rmlui_document_*` call takes an element handle that must be a document (anything else is ignored).

// Load an .rml file (RmlUi opens it, relative to the working directory) or RML text. NOT SHOWN: call `show`.
// `source_url` names text-loaded documents in the log and is what relative `src`/`href` paths resolve against.
// 0 when it could not be loaded — the log says why.
int32_t kama_rmlui_document_load(int32_t context, const char* path);
int32_t kama_rmlui_document_load_rml(int32_t context, const char* rml, const char* source_url);

// `modal`: 0 none, 1 modal, 2 keep. `focus`: 0 none, 1 the document, 2 keep the last focus, 3 auto (the first element
// with `autofocus`, else the document) — RmlUi's ModalFlag and FocusFlag.
void    kama_rmlui_document_show(int32_t document, int32_t modal, int32_t focus);
void    kama_rmlui_document_hide(int32_t document);
// ⚠️ THE CLOSE LANDS AT THE NEXT `context_update`, as RmlUi's does; the handle is dead from then. Listeners on its
// elements are detached by RmlUi itself (EventListener::OnDetach) as they go, which is what removed the games'
// "detach before destroying or it is a use-after-free next frame" ordering rule.
void    kama_rmlui_document_close(int32_t document);
int32_t kama_rmlui_document_title(int32_t document);
int32_t kama_rmlui_document_context(int32_t document);
// The next element in TAB order after `from` (the document itself for "the first"), forward or backward, wrapping or
// not. 0 when there is none. What lets a pad's first press land on the first control rather than be lost.
int32_t kama_rmlui_document_next_tab(int32_t document, int32_t from, int32_t forward, int32_t wrap);

// ---- elements ----------------------------------------------------------------------------------------------------------

int32_t kama_rmlui_element_is_live(int32_t element);
// Search under `scope` (an element or a document): by id, or the first match of a CSS selector. 0 when none.
int32_t kama_rmlui_element_by_id(int32_t scope, const char* id);
int32_t kama_rmlui_element_query(int32_t scope, const char* selector);
int32_t kama_rmlui_element_owner_document(int32_t element);
int32_t kama_rmlui_element_parent(int32_t element);
int32_t kama_rmlui_element_child_count(int32_t element);
int32_t kama_rmlui_element_child(int32_t element, int32_t index);
// The deepest focused element under this one (RmlUi's GetFocusLeafNode): the element itself when nothing inside it
// has focus. On a document, "the document" means NOTHING IN IT IS FOCUSED.
int32_t kama_rmlui_element_focus_leaf(int32_t element);

// String getters (length, or -1; the text is in the scratch buffer).
int32_t kama_rmlui_element_tag(int32_t element);
int32_t kama_rmlui_element_id(int32_t element);
int32_t kama_rmlui_element_inner_rml(int32_t element);
// The text of every text node under the element, in document order, entities decoded.
int32_t kama_rmlui_element_text(int32_t element);
// -1 when the attribute is absent (distinct from present and empty).
int32_t kama_rmlui_element_attribute(int32_t element, const char* name);
// The element's computed value of one property, as RCSS text. -1 for an unknown property.
int32_t kama_rmlui_element_property(int32_t element, const char* name);
// A form control's value (<input>, <textarea>, <select>); -1 when the element is not a form control.
int32_t kama_rmlui_element_value(int32_t element);

// ⚠️ REPLACES the element's children by parsing markup, so every handle to an element inside it is DEAD afterwards
// (correctly — see the top of this file); look the new ones up after the write. ⚠️ It is a PARSER: text another
// person typed goes through `kama_rmlui_element_set_text` or `kama_rmlui_encode_rml` first, never straight in.
void    kama_rmlui_element_set_inner_rml(int32_t element, const char* rml);
// Replace the children with one text node holding `text` verbatim (escaped with RmlUi's own EncodeRml).
void    kama_rmlui_element_set_text(int32_t element, const char* text);
void    kama_rmlui_element_set_attribute(int32_t element, const char* name, const char* value);
void    kama_rmlui_element_remove_attribute(int32_t element, const char* name);
int32_t kama_rmlui_element_has_class(int32_t element, const char* name);
void    kama_rmlui_element_set_class(int32_t element, const char* name, int32_t on);
// One inline style property ("width", "43%"). An empty value removes it. 1 when RmlUi accepted it.
int32_t kama_rmlui_element_set_property(int32_t element, const char* name, const char* value);
// Set a form control's value. ⚠️ RmlUi RAISES A `change` EVENT FOR THIS, AND A BROWSER'S `el.value = …` DOES NOT
// (measured by friendly-fire-department's uispike): `quiet` 1 drops that event, so only a person's edit is queued.
// 1 when the element is a form control.
int32_t kama_rmlui_element_set_value(int32_t element, const char* value, int32_t quiet);

// The element's box after layout, absolute, in framebuffer pixels. `area`: 0 margin, 1 border, 2 padding, 3 content.
// Returns 1, or 0 for a dead handle (all four zeroed — written on every path).
int32_t kama_rmlui_element_rect(int32_t element, int32_t area, float* x, float* y, float* width, float* height);
int32_t kama_rmlui_element_is_visible(int32_t element);
// Focus it (1 when it took focus; `visible` shows a focus ring, as for keyboard navigation), blur it, click it.
int32_t kama_rmlui_element_focus(int32_t element, int32_t visible);
void    kama_rmlui_element_blur(int32_t element);
void    kama_rmlui_element_click(int32_t element);
void    kama_rmlui_element_scroll_into_view(int32_t element);

// RmlUi's own escaping (StringUtilities::EncodeRml: < > & "), into the scratch buffer.
int32_t kama_rmlui_encode_rml(const char* text);

// ---- events out ----------------------------------------------------------------------------------------------------------

// The event types the poll names; anything else (a custom event) is KAMA_RMLUI_EV_OTHER.
#define KAMA_RMLUI_EV_OTHER          0
#define KAMA_RMLUI_EV_CLICK          1
#define KAMA_RMLUI_EV_DBLCLICK       2
#define KAMA_RMLUI_EV_MOUSEDOWN      3
#define KAMA_RMLUI_EV_MOUSEUP        4
#define KAMA_RMLUI_EV_MOUSEMOVE      5
#define KAMA_RMLUI_EV_MOUSEOVER      6
#define KAMA_RMLUI_EV_MOUSEOUT       7
#define KAMA_RMLUI_EV_MOUSESCROLL    8
#define KAMA_RMLUI_EV_FOCUS          9
#define KAMA_RMLUI_EV_BLUR           10
#define KAMA_RMLUI_EV_KEYDOWN        11
#define KAMA_RMLUI_EV_KEYUP          12
#define KAMA_RMLUI_EV_TEXTINPUT      13
#define KAMA_RMLUI_EV_CHANGE         14
#define KAMA_RMLUI_EV_SUBMIT         15
#define KAMA_RMLUI_EV_SHOW           16
#define KAMA_RMLUI_EV_HIDE           17
#define KAMA_RMLUI_EV_LOAD           18
#define KAMA_RMLUI_EV_UNLOAD         19
#define KAMA_RMLUI_EV_SCROLL         20
#define KAMA_RMLUI_EV_RESIZE         21
#define KAMA_RMLUI_EV_TABCHANGE      22
#define KAMA_RMLUI_EV_DRAGSTART      23
#define KAMA_RMLUI_EV_DRAG           24
#define KAMA_RMLUI_EV_DRAGEND        25
#define KAMA_RMLUI_EV_DRAGOVER       26
#define KAMA_RMLUI_EV_DRAGDROP       27
#define KAMA_RMLUI_EV_DRAGOUT        28
#define KAMA_RMLUI_EV_DRAGMOVE       29
#define KAMA_RMLUI_EV_HANDLEDRAG     30
#define KAMA_RMLUI_EV_ANIMATIONEND   31
#define KAMA_RMLUI_EV_TRANSITIONEND  32

// Listen for `event` ("click", "change", "keydown", or any custom name) on an element. Returns a listener handle
// (>= 1), or 0 for a dead element or one in no context. `flags`: 1 listen in the CAPTURE phase, 2 stop the event's propagation here (it is
// decided synchronously, inside RmlUi's dispatch, because a queued event is read too late to stop anything).
//
// ⚠️ RmlUi BUBBLES, so a listener on a container hears its children's events too — one press on a button inside a
// listened-to container is TWO events (the button's and the container's), with the same `target`. That is the
// browser's model as well, and it is what the two backends of @kama/ui both preserve.
//
// A listener goes away with its element (RmlUi detaches it; the handle is dead from then) or on `unlisten`.
#define KAMA_RMLUI_LISTEN_CAPTURE 1
#define KAMA_RMLUI_LISTEN_STOP    2
int32_t kama_rmlui_listen(int32_t element, const char* event, int32_t flags);
void    kama_rmlui_unlisten(int32_t listener);

// Pop the oldest event queued in THIS CONTEXT (each context has its own queue, so two UIs in one program never see
// each other's events): returns 1 and writes the listener that heard it, its KAMA_RMLUI_EV_* type, the element the
// listener is on, the event's TARGET (where it happened) and the BINDING the listener came from (0 for a plain
// listener; see "bindings"), or returns 0 when the queue is empty (all five written as 0). Every output is written on
// every path.
int32_t kama_rmlui_poll_event(int32_t context, int32_t* listener, int32_t* type, int32_t* element, int32_t* target,
                              int32_t* binding);
// Drop every event queued in this context (a screen change, where queued events name elements that are going).
void    kama_rmlui_clear_events(int32_t context);

// ---- bindings: an id you can hold across rewrites ---------------------------------------------------------------------
//
// An element handle dies with its element, which is right for a handle — but a UI that rewrites a list with
// `set_inner_rml` usually wants to keep talking about "the row with id `bind3`", whichever element that is now. A
// BINDING is that: (document, id) -> a handle that is the SAME for the same pair while the document lives, that
// re-finds its element by id whenever the one it last found has been destroyed, and whose listeners FOLLOW it — they
// are re-attached to the new element when the binding re-finds it. Events heard through a binding report it.
//
// Re-finding is lazy: it happens when the binding is used (`bind` again, `binding_element`, `binding_listen`,
// `binding_of`). So after a rewrite, touch the bindings of what you wrote — "re-bind what you wrote, after the write",
// the rule both games already lived by — and their listeners are live again. (The games' shims re-pointed bindings
// with a write counter and moved listeners by hand; this is that, made general.) A binding dies with its document.
//
// `bind` returns 0 when the document is dead or — on the FIRST bind of an id — when no element has that id.
int32_t kama_rmlui_bind(int32_t document, const char* id);
// The binding's element now (re-finding it if need be), or 0 when no element has the id at the moment.
int32_t kama_rmlui_binding_element(int32_t binding);
// Listen through the binding (flags as `kama_rmlui_listen`). Idempotent per (event, flags): binding twice and listening
// twice attaches once. 1 when the binding is live.
int32_t kama_rmlui_binding_listen(int32_t binding, const char* event, int32_t flags);
// The binding of `document` whose element is `element` now, or 0. What maps "the focused element" back to a binding.
int32_t kama_rmlui_binding_of(int32_t document, int32_t element);
int32_t kama_rmlui_binding_id(int32_t binding);

// The LAST POPPED event's parameters, valid until the next poll: the event's own name, and any parameter RmlUi
// attached ("key_identifier", "button", "mouse_x", "value", "shift_key", …). The int and float readers return
// `fallback` for an absent parameter; the string reader returns -1.
int32_t kama_rmlui_event_name(void);
int32_t kama_rmlui_event_param_int(const char* name, int32_t fallback);
float   kama_rmlui_event_param_float(const char* name, float fallback);
int32_t kama_rmlui_event_param_string(const char* name);

// ---- reading back the recorded draw commands --------------------------------------------------------------------------
//
// Per context, valid from one `context_render` until the next `context_update` or `context_render` of ANY context
// (an update may release geometry a command points at). Every accessor is total: an out-of-range index or a dead
// context yields zeroes and NULL, so a caller whose loop bounds are wrong misdraws instead of crashing.
//
// Commands are in DRAW ORDER and must be applied in order: a clip-mask command changes the state of every draw after it.
#define KAMA_RMLUI_CMD_DRAW            1   // draw the geometry (translation, texture, scissor, transform)
#define KAMA_RMLUI_CMD_CLIP_ENABLE     2   // from here on, draws are clipped by the clip mask
#define KAMA_RMLUI_CMD_CLIP_DISABLE    3   // from here on, they are not
#define KAMA_RMLUI_CMD_CLIP_SET        4   // the mask becomes the area of this geometry (cleared first)
#define KAMA_RMLUI_CMD_CLIP_SET_INVERSE 5  // the mask becomes the area OUTSIDE this geometry (cleared first)
#define KAMA_RMLUI_CMD_CLIP_INTERSECT  6   // the mask is intersected with this geometry
// (RmlUi's layers, filters and shaders — box-shadow, filter, gradients — are not recorded: RmlUi skips what the
// interface does not implement, so those decorations do not appear.)

int32_t kama_rmlui_command_count(int32_t context);
int32_t kama_rmlui_command_kind(int32_t context, int32_t i);

// Vertices are a FLAT float array, 8 floats per vertex — x, y, u, v, r, g, b, a — rather than RmlUi's 20-byte
// `Rml::Vertex` with four packed bytes of colour. Colour stays PREMULTIPLIED, in 0..1. Read-only.
const float*   kama_rmlui_command_vertices(int32_t context, int32_t i);
int32_t        kama_rmlui_command_vertex_count(int32_t context, int32_t i);
const int32_t* kama_rmlui_command_indices(int32_t context, int32_t i);
int32_t        kama_rmlui_command_index_count(int32_t context, int32_t i);
// A number for the geometry, never reused for different geometry: RmlUi compiles geometry once and re-renders it
// frame after frame, so a renderer may cache its upload keyed on this.
int32_t        kama_rmlui_command_geometry(int32_t context, int32_t i);
// The texture this draw samples, or 0 for untextured geometry (a `kama_rmlui_texture_*` handle).
int32_t        kama_rmlui_command_texture(int32_t context, int32_t i);
// Translation to add to every vertex position, in pixels.
float          kama_rmlui_command_translate_x(int32_t context, int32_t i);
float          kama_rmlui_command_translate_y(int32_t context, int32_t i);
// Writes the scissor rectangle and returns 1 when this command is scissored; 0 (and zeroes) when it is not.
int32_t        kama_rmlui_command_scissor(int32_t context, int32_t i, int32_t* x, int32_t* y, int32_t* width,
                                          int32_t* height);
// Writes the 4x4 transform, COLUMN-MAJOR (16 floats), applied after the translation, and returns 1; returns 0 (and
// writes the identity) when no transform applies.
int32_t        kama_rmlui_command_transform(int32_t context, int32_t i, float* out16);

// ---- textures --------------------------------------------------------------------------------------------------------
//
// Global, shared by every context: font atlases RmlUi generates during layout, and provided images once displayed.
// RGBA8, PREMULTIPLIED, tightly packed. Handles 1..count; a released slot reads as 0x0 with no data.
//
// ⚠️ A HANDLE IS AN INDEX AND A RELEASED SLOT IS RECYCLED, so one handle can hold a different atlas later — and
// neither the size nor the data pointer can tell you (two glyph atlases are often the same size, and a freed buffer is
// often reallocated at the same address; the-mallow-man shipped garbled text from exactly that). The GENERATION is
// bumped every time a slot is filled: a renderer that maps these to GPU textures must key its upload on it.
int32_t        kama_rmlui_texture_count(void);
int32_t        kama_rmlui_texture_width(int32_t texture);
int32_t        kama_rmlui_texture_height(int32_t texture);
const uint8_t* kama_rmlui_texture_data(int32_t texture);
int32_t        kama_rmlui_texture_generation(int32_t texture);

#ifdef __cplusplus
}
#endif

#endif  // KAMA_RMLUI_H
