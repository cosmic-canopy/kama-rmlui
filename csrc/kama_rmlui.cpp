// kama_rmlui.cpp — RmlUi 6.3, recording rather than drawing. Read kama_rmlui.h first: it carries the design.
//
// From the-mallow-man's ui/src/tmm_ui_native.cpp (E7b/E7c, 2026-10-07), itself from friendly-fire-department@a676ca3's
// ffd_ui_native.cpp. What changed on the way from a game's shim to a general binding:
//   - Contexts are a handle table, not one file-static context; documents are any number per context.
//   - Elements are handles for ANY element, tracked by RmlUi's ObserverPtr (dead, never dangling), instead of a
//     bind-by-id table repaired by a write counter. The games' `bind` (id -> stable handle) is @kama/ui's policy now.
//   - Listeners are on any event and any element, and RmlUi's own EventListener::OnDetach retires them, which removes
//     the "detach every listener before closing the document" ordering both games had to get right by hand.
//   - The render interface records clip masks and transforms too (the games recorded draws and scissors only).
//   - Keys are RmlUi's own KeyIdentifiers. The games' GLFW-numbered subset (and the reasons it is a subset) belong to
//     the seam that has a keyboard of its own to reconcile, which is @kama/ui.
//   - Strings come back through one scratch buffer; RmlUi's log is a queue as well as stdout.
//
// This is the ONLY file that includes an RmlUi header. It compiles as C++17 (kama's `csources` default, gnu++17).

// ⚠️ THE ARCHIVE native/Makefile BUILDS IS STATIC ON EVERY PLATFORM, so RmlUi's headers must be told so before they
// are read: on Windows every call is otherwise a `__declspec(dllimport)` and the link fails on `__imp__ZN3Rml…`
// against a perfectly good librmlui.a (friendly-fire-department). Defined here rather than as a manifest flag,
// because a dependency's `cflags` reach every translation unit of the consumer's build.
#ifndef RMLUI_STATIC_LIB
#define RMLUI_STATIC_LIB 1
#endif

#include "kama_rmlui.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

// The numbers kama spells as literals (src/keys.kama, src/rmlui.kama). An RmlUi upgrade that moved one fails HERE,
// by name, rather than as a key that silently does something else.
static_assert(Rml::Input::KI_SPACE == 1 && Rml::Input::KI_0 == 2 && Rml::Input::KI_9 == 11, "kama keys: digits");
static_assert(Rml::Input::KI_A == 12 && Rml::Input::KI_Z == 37, "kama keys: letters");
static_assert(Rml::Input::KI_BACK == 69 && Rml::Input::KI_TAB == 70 && Rml::Input::KI_RETURN == 72, "kama keys: editing");
static_assert(Rml::Input::KI_ESCAPE == 81 && Rml::Input::KI_PRIOR == 86 && Rml::Input::KI_NEXT == 87, "kama keys: paging");
static_assert(Rml::Input::KI_END == 88 && Rml::Input::KI_HOME == 89, "kama keys: home/end");
static_assert(Rml::Input::KI_LEFT == 90 && Rml::Input::KI_UP == 91 && Rml::Input::KI_RIGHT == 92 &&
              Rml::Input::KI_DOWN == 93, "kama keys: arrows");
static_assert(Rml::Input::KI_INSERT == 98 && Rml::Input::KI_DELETE == 99, "kama keys: insert/delete");
static_assert(Rml::Input::KI_F1 == 107 && Rml::Input::KI_F12 == 118, "kama keys: function keys");
static_assert(Rml::Input::KI_NUMPADENTER == 61, "kama keys: numpad enter");
static_assert(Rml::Input::KM_CTRL == KAMA_RMLUI_MOD_CTRL && Rml::Input::KM_SHIFT == KAMA_RMLUI_MOD_SHIFT &&
              Rml::Input::KM_ALT == KAMA_RMLUI_MOD_ALT && Rml::Input::KM_META == KAMA_RMLUI_MOD_META &&
              Rml::Input::KM_CAPSLOCK == KAMA_RMLUI_MOD_CAPSLOCK && Rml::Input::KM_NUMLOCK == KAMA_RMLUI_MOD_NUMLOCK &&
              Rml::Input::KM_SCROLLLOCK == KAMA_RMLUI_MOD_SCROLLLOCK, "kama modifiers");
static_assert(static_cast<int>(Rml::BoxArea::Margin) == 0 && static_cast<int>(Rml::BoxArea::Content) == 3, "box areas");
static_assert(static_cast<int>(Rml::ModalFlag::Keep) == 2 && static_cast<int>(Rml::FocusFlag::Auto) == 3, "show flags");
// `kama_rmlui_command_transform` hands out `data()` as column-major; that holds only for RmlUi's default matrix.
static_assert(std::is_same<Rml::Matrix4f, Rml::ColumnMajorMatrix4f>::value, "RmlUi built with RMLUI_MATRIX_ROW_MAJOR");

namespace {

// ---- handles ---------------------------------------------------------------------------------------------------------
//
// A handle is (generation << 20) | (slot + 1): 20 bits of slot, 11 of generation, always positive, 0 never valid.
// A slot is recycled FIFO and its generation bumped, so an old handle stops matching the moment its slot is reused —
// and, FIFO, a slot comes round again only after every other free one, so a generation cannot wrap under a handle
// anybody still holds in practice.
template <typename T>
class HandleTable {
public:
    int32_t add(T value)
    {
        uint32_t slot;
        if (!free_.empty() && slots_.size() >= kMinBeforeReuse) {
            slot = free_.front();
            free_.pop_front();
        } else {
            slot = static_cast<uint32_t>(slots_.size());
            if (slot >= kMaxSlots) { return 0; }
            slots_.push_back(Slot());
        }
        Slot& s = slots_[slot];
        s.value = std::move(value);
        s.live = true;
        return encode(slot, s.generation);
    }

    T* get(int32_t handle)
    {
        uint32_t slot;
        if (!decode(handle, slot)) { return nullptr; }
        return &slots_[slot].value;
    }

    void remove(int32_t handle)
    {
        uint32_t slot;
        if (!decode(handle, slot)) { return; }
        Slot& s = slots_[slot];
        s.live = false;
        s.value = T();
        s.generation = (s.generation + 1) & kGenMask;
        free_.push_back(slot);
    }

    // Every live handle, for a sweep.
    template <typename F> void each(F f)
    {
        for (uint32_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].live) { f(encode(i, slots_[i].generation), slots_[i].value); }
        }
    }

    size_t size() const { return slots_.size(); }
    size_t freeCount() const { return free_.size(); }

    void clear()
    {
        slots_.clear();
        free_.clear();
    }

private:
    static constexpr uint32_t kSlotBits = 20;
    static constexpr uint32_t kMaxSlots = (1u << kSlotBits) - 1;
    static constexpr uint32_t kGenMask = (1u << 11) - 1;
    // A small table never recycles: the first few hundred handles are unique outright, which is all most UIs use.
    static constexpr size_t kMinBeforeReuse = 256;

    struct Slot {
        T        value{};
        uint32_t generation = 0;
        bool     live = false;
    };

    static int32_t encode(uint32_t slot, uint32_t gen)
    {
        return static_cast<int32_t>((gen << kSlotBits) | (slot + 1));
    }

    bool decode(int32_t handle, uint32_t& slot) const
    {
        if (handle <= 0) { return false; }
        const uint32_t h = static_cast<uint32_t>(handle);
        const uint32_t s = (h & ((1u << kSlotBits) - 1));
        if (s == 0 || s > slots_.size()) { return false; }
        slot = s - 1;
        const Slot& sl = slots_[slot];
        return sl.live && sl.generation == (h >> kSlotBits);
    }

    std::vector<Slot>    slots_;
    std::deque<uint32_t> free_;
};

// ---- recorded geometry -----------------------------------------------------------------------------------------------

// One compiled geometry, expanded to the flat layout kama_rmlui.h documents. Copied even though RmlUi keeps its own
// data valid until release: the point of the seam is that kama reads a stable array in the layout it wants.
struct Geometry {
    std::vector<float>   vertices;   // 8 floats per vertex: x, y, u, v, r, g, b, a
    std::vector<int32_t> indices;
    int32_t              id = 0;     // never reused (kama_rmlui_command_geometry)
    bool                 live = false;
};

struct Command {
    int32_t kind = KAMA_RMLUI_CMD_DRAW;
    size_t  geometry = 0;            // slot in g_geometry, or SIZE_MAX for a command with none
    float   translate_x = 0.0f;
    float   translate_y = 0.0f;
    int32_t texture = 0;
    bool    scissor_enabled = false;
    int32_t scissor_x = 0, scissor_y = 0, scissor_w = 0, scissor_h = 0;
    int32_t transform = -1;          // index into the context's per-frame transforms, -1 for none
};

struct Matrix16 { float m[16]; };

struct QueuedEvent {
    int32_t         listener = 0;
    int32_t         type = KAMA_RMLUI_EV_OTHER;
    int32_t         element = 0;
    int32_t         target = 0;
    int32_t         binding = 0;
    Rml::String     name;
    Rml::Dictionary params;
};

struct ContextRec {
    Rml::Context*         context = nullptr;
    std::string           name;
    std::vector<Command>  commands;     // the last render
    std::vector<Matrix16> transforms;   // the last render's distinct transforms
    std::deque<QueuedEvent> events;     // this context's queue (kama_rmlui_poll_event)
};

struct Texture {
    std::vector<uint8_t> pixels;     // RGBA8, premultiplied, tightly packed
    int32_t              width = 0;
    int32_t              height = 0;
    // ⚠️ BUMPED EVERY TIME A SLOT IS FILLED — the only reliable way to see a recycled one (kama_rmlui.h, "textures").
    // Comparing (width, height, data) missed roughly half the recycles in a real run of the-mallow-man.
    int32_t              generation = 0;
};

// One image the caller provided, waiting for a document to ask for it by name. SEPARATE from g_textures on purpose:
// minting the texture slot at provide time would put a picture nobody displays into the texture count, and a
// renderer registers a GPU texture for every slot it sees.
struct ProvidedImage {
    std::string          name;
    std::vector<uint8_t> pixels;
    int32_t              width = 0;
    int32_t              height = 0;
    int32_t              handle = 0;   // the g_textures handle, minted on first request
};

struct ElementRec {
    Rml::ObserverPtr<Rml::Element> ptr;
    Rml::Element*                  key = nullptr;   // the address it was registered under, for g_element_ids
};

class QueueingListener;

// One (event, flags) a binding listens for, re-attached whenever the binding re-finds its element.
struct ListenSpec {
    Rml::String event;
    int32_t     flags = 0;
};

struct BindingRec {
    int32_t                 document = 0;
    Rml::String             id;
    int32_t                 element = 0;   // the element it found last (may be dead: then it re-finds)
    std::vector<ListenSpec> listens;
};


struct LogLine {
    int32_t     type = 0;
    std::string text;
};

// ---- the state ---------------------------------------------------------------------------------------------------------
//
// Geometry and textures outlive a frame (RmlUi compiles once and re-renders by handle), so they are NOT cleared per
// render; a context's commands are. Geometry and texture handles are plain 1-based indices because RmlUi, not kama,
// holds them — and texture slots carry their own generation for kama's sake.

std::vector<Geometry> g_geometry;
std::vector<size_t>   g_free_geometry;
int32_t               g_next_geometry_id = 1;
std::vector<Texture>  g_textures;
std::vector<size_t>   g_free_textures;
std::vector<ProvidedImage> g_provided;
std::vector<std::vector<uint8_t>> g_font_memory;   // faces loaded from memory: RmlUi reads them until shutdown

HandleTable<ContextRec*>                    g_contexts;
HandleTable<ElementRec>                     g_elements;
std::unordered_map<Rml::Element*, int32_t>  g_element_ids;
size_t                                      g_elements_at_sweep = 0;
HandleTable<QueueingListener*>              g_listeners;
HandleTable<BindingRec>                     g_bindings;
std::unordered_map<std::string, int32_t>    g_binding_ids;   // "<document>:<id>" -> binding

QueuedEvent             g_last_event;
std::deque<LogLine>     g_log;
bool                    g_log_echo = true;
std::string             g_scratch;

int                     g_refs = 0;
int32_t                 g_context_counter = 0;
// ⚠️ SET BEFORE `Rml::Shutdown()`. Tearing RmlUi down releases every texture and listener, which is normal and must be
// quiet; a release at any OTHER time is what the provided-image warning in ReleaseTexture is for.
bool                    g_shutting_down = false;
// Set while a setter writes a form control's value with `quiet`, so the Change RmlUi raises for it is not queued.
bool                    g_quiet_change = false;

// The context being rendered (RenderGeometry & co. only happen inside Context::Render), and the render state RmlUi
// sets as MODES — EnableScissorRegion / SetScissorRegion / SetTransform apply to every draw after them — tracked here
// and snapshot into each command as it is recorded.
ContextRec* g_recording = nullptr;
bool        g_scissor_enabled = false;
int32_t     g_scissor_x = 0, g_scissor_y = 0, g_scissor_w = 0, g_scissor_h = 0;
int32_t     g_transform = -1;

int32_t scratch(const std::string& s)
{
    g_scratch = s;
    return static_cast<int32_t>(g_scratch.size());
}

// ---- element handles ---------------------------------------------------------------------------------------------------

// Forget every element whose observer went null — its slot is recycled and its address unmapped. Amortised: run when
// the table has grown by half since the last sweep, so a UI that rewrites a list every frame does not grow it forever.
void sweepElements()
{
    std::vector<int32_t> dead;
    g_elements.each([&](int32_t h, ElementRec& r) {
        if (r.ptr.get() == nullptr) { dead.push_back(h); }
    });
    for (int32_t h : dead) {
        ElementRec* r = g_elements.get(h);
        if (r == nullptr) { continue; }
        auto it = g_element_ids.find(r->key);
        if (it != g_element_ids.end() && it->second == h) { g_element_ids.erase(it); }
        g_elements.remove(h);
    }
    g_elements_at_sweep = g_elements.size() - g_elements.freeCount();
}

// The handle for an element, minting one the first time it is seen. The same LIVE element always answers the same.
int32_t handleOf(Rml::Element* e)
{
    if (e == nullptr) { return 0; }
    auto it = g_element_ids.find(e);
    if (it != g_element_ids.end()) {
        ElementRec* r = g_elements.get(it->second);
        // ⚠️ THE ADDRESS ALONE PROVES NOTHING: a destroyed element's memory is routinely reused by the one replacing it.
        // The observer is what says whether the element this handle was minted for still exists.
        if (r != nullptr && r->ptr.get() == e) { return it->second; }
        if (r != nullptr && r->ptr.get() == nullptr) { g_elements.remove(it->second); }
        g_element_ids.erase(it);
    }
    const size_t liveNow = g_elements.size() - g_elements.freeCount();
    if (liveNow > 512 && liveNow > g_elements_at_sweep + g_elements_at_sweep / 2) { sweepElements(); }

    ElementRec rec;
    rec.ptr = e->GetObserverPtr();
    rec.key = e;
    const int32_t h = g_elements.add(rec);
    if (h != 0) { g_element_ids[e] = h; }
    return h;
}

Rml::Element* elementAt(int32_t handle)
{
    ElementRec* r = g_elements.get(handle);
    return (r == nullptr) ? nullptr : r->ptr.get();
}

Rml::ElementDocument* documentAt(int32_t handle)
{
    return rmlui_dynamic_cast<Rml::ElementDocument*>(elementAt(handle));
}

ContextRec* contextAt(int32_t handle)
{
    ContextRec** r = g_contexts.get(handle);
    return (r == nullptr) ? nullptr : *r;
}

int32_t contextHandleOf(Rml::Context* c)
{
    int32_t found = 0;
    g_contexts.each([&](int32_t h, ContextRec*& r) {
        if (r != nullptr && r->context == c) { found = h; }
    });
    return found;
}

int32_t eventTypeOf(Rml::EventId id)
{
    switch (id) {
        case Rml::EventId::Click:         return KAMA_RMLUI_EV_CLICK;
        case Rml::EventId::Dblclick:      return KAMA_RMLUI_EV_DBLCLICK;
        case Rml::EventId::Mousedown:     return KAMA_RMLUI_EV_MOUSEDOWN;
        case Rml::EventId::Mouseup:       return KAMA_RMLUI_EV_MOUSEUP;
        case Rml::EventId::Mousemove:     return KAMA_RMLUI_EV_MOUSEMOVE;
        case Rml::EventId::Mouseover:     return KAMA_RMLUI_EV_MOUSEOVER;
        case Rml::EventId::Mouseout:      return KAMA_RMLUI_EV_MOUSEOUT;
        case Rml::EventId::Mousescroll:   return KAMA_RMLUI_EV_MOUSESCROLL;
        case Rml::EventId::Focus:         return KAMA_RMLUI_EV_FOCUS;
        case Rml::EventId::Blur:          return KAMA_RMLUI_EV_BLUR;
        case Rml::EventId::Keydown:       return KAMA_RMLUI_EV_KEYDOWN;
        case Rml::EventId::Keyup:         return KAMA_RMLUI_EV_KEYUP;
        case Rml::EventId::Textinput:     return KAMA_RMLUI_EV_TEXTINPUT;
        case Rml::EventId::Change:        return KAMA_RMLUI_EV_CHANGE;
        case Rml::EventId::Submit:        return KAMA_RMLUI_EV_SUBMIT;
        case Rml::EventId::Show:          return KAMA_RMLUI_EV_SHOW;
        case Rml::EventId::Hide:          return KAMA_RMLUI_EV_HIDE;
        case Rml::EventId::Load:          return KAMA_RMLUI_EV_LOAD;
        case Rml::EventId::Unload:        return KAMA_RMLUI_EV_UNLOAD;
        case Rml::EventId::Scroll:        return KAMA_RMLUI_EV_SCROLL;
        case Rml::EventId::Resize:        return KAMA_RMLUI_EV_RESIZE;
        case Rml::EventId::Tabchange:     return KAMA_RMLUI_EV_TABCHANGE;
        case Rml::EventId::Dragstart:     return KAMA_RMLUI_EV_DRAGSTART;
        case Rml::EventId::Drag:          return KAMA_RMLUI_EV_DRAG;
        case Rml::EventId::Dragend:       return KAMA_RMLUI_EV_DRAGEND;
        case Rml::EventId::Dragover:      return KAMA_RMLUI_EV_DRAGOVER;
        case Rml::EventId::Dragdrop:      return KAMA_RMLUI_EV_DRAGDROP;
        case Rml::EventId::Dragout:       return KAMA_RMLUI_EV_DRAGOUT;
        case Rml::EventId::Dragmove:      return KAMA_RMLUI_EV_DRAGMOVE;
        case Rml::EventId::Handledrag:    return KAMA_RMLUI_EV_HANDLEDRAG;
        case Rml::EventId::Animationend:  return KAMA_RMLUI_EV_ANIMATIONEND;
        case Rml::EventId::Transitionend: return KAMA_RMLUI_EV_TRANSITIONEND;
        default:                          return KAMA_RMLUI_EV_OTHER;
    }
}

// ---- the interfaces RmlUi requires -----------------------------------------------------------------------------------

class RecordingRenderer final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override
    {
        Geometry g;
        g.vertices.reserve(vertices.size() * 8);
        for (const Rml::Vertex& v : vertices) {
            g.vertices.push_back(v.position.x);
            g.vertices.push_back(v.position.y);
            g.vertices.push_back(v.tex_coord.x);
            g.vertices.push_back(v.tex_coord.y);
            // Premultiplied bytes -> 0..1 floats, and premultiplied it stays: straight alpha here would lose the
            // reason RmlUi hands it over that way, and the consumer's blend state is what has to match.
            g.vertices.push_back(static_cast<float>(v.colour.red)   / 255.0f);
            g.vertices.push_back(static_cast<float>(v.colour.green) / 255.0f);
            g.vertices.push_back(static_cast<float>(v.colour.blue)  / 255.0f);
            g.vertices.push_back(static_cast<float>(v.colour.alpha) / 255.0f);
        }
        g.indices.assign(indices.begin(), indices.end());
        g.id = g_next_geometry_id++;
        g.live = true;

        // Reuse a released slot before growing: RmlUi recompiles on every layout change, so an animating UI churns
        // through these. Handles are indices, so slots can be recycled but never compacted.
        if (!g_free_geometry.empty()) {
            const size_t slot = g_free_geometry.back();
            g_free_geometry.pop_back();
            g_geometry[slot] = std::move(g);
            return static_cast<Rml::CompiledGeometryHandle>(slot + 1);
        }
        g_geometry.push_back(std::move(g));
        return static_cast<Rml::CompiledGeometryHandle>(g_geometry.size());   // 1-based: 0 stays "none"
    }

    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override
    {
        record(KAMA_RMLUI_CMD_DRAW, geometry, translation, static_cast<int32_t>(texture));
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override
    {
        if (geometry == 0 || geometry > g_geometry.size()) { return; }
        const size_t slot = static_cast<size_t>(geometry) - 1;
        Geometry& g = g_geometry[slot];
        if (!g.live) { return; }   // a double release must not corrupt the free list
        g.live = false;
        g.vertices.clear();
        g.vertices.shrink_to_fit();
        g.indices.clear();
        g.indices.shrink_to_fit();
        g_free_geometry.push_back(slot);
    }

    // An image a document asked for. This shim opens NO FILES: it matches the resolved path against the provided
    // images (kama_rmlui.h, "provided images") and mints a texture slot on first use.
    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override
    {
        for (ProvidedImage& img : g_provided) {
            if (img.name != source) { continue; }
            texture_dimensions = Rml::Vector2i(img.width, img.height);
            if (img.handle != 0) { return static_cast<Rml::TextureHandle>(img.handle); }
            Texture t;
            t.width = img.width;
            t.height = img.height;
            t.pixels = img.pixels;
            img.handle = static_cast<int32_t>(addTexture(std::move(t)));
            return static_cast<Rml::TextureHandle>(img.handle);
        }
        // Nothing under that name. Say so rather than invent a handle: RmlUi lays the element out at zero size, and it
        // records the failure PERMANENTLY, so this line is the one warning anybody gets.
        Rml::Log::Message(Rml::Log::LT_WARNING, "kama_rmlui: LoadTexture(\"%s\") — no image was provided under that name",
                          source.c_str());
        return 0;
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override
    {
        Texture t;
        t.width = dimensions.x;
        t.height = dimensions.y;
        t.pixels.assign(source.begin(), source.end());
        return static_cast<Rml::TextureHandle>(addTexture(std::move(t)));
    }

    void ReleaseTexture(Rml::TextureHandle texture) override
    {
        if (texture == 0 || texture > g_textures.size()) { return; }
        const size_t slot = static_cast<size_t>(texture) - 1;
        Texture& t = g_textures[slot];
        if (t.width == 0 && t.height == 0 && t.pixels.empty()) { return; }   // already released

        // ⚠️ A PROVIDED IMAGE'S SLOT IS EXPECTED TO LIVE FOR THE PROCESS: RmlUi 6.3 caches file textures by source string
        // and releases them only at shutdown (measured across document switches: zero releases). If a future RmlUi
        // changes that, the slot is recycled under a renderer still drawing the old picture — this is the one
        // sentence that says so.
        if (!g_shutting_down) {
            for (ProvidedImage& img : g_provided) {
                if (img.handle == static_cast<int32_t>(texture)) {
                    Rml::Log::Message(Rml::Log::LT_WARNING,
                                      "kama_rmlui: provided image \"%s\" was released mid-run (texture %d); a renderer "
                                      "keyed on the handle alone is now stale — key on the generation", img.name.c_str(),
                                      img.handle);
                    img.handle = 0;
                    break;
                }
            }
        }
        t.pixels.clear();
        t.pixels.shrink_to_fit();
        t.width = 0;
        t.height = 0;
        g_free_textures.push_back(slot);
    }

    void EnableScissorRegion(bool enable) override { g_scissor_enabled = enable; }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        g_scissor_x = region.Left();
        g_scissor_y = region.Top();
        g_scissor_w = region.Width();
        g_scissor_h = region.Height();
    }

    void EnableClipMask(bool enable) override
    {
        record(enable ? KAMA_RMLUI_CMD_CLIP_ENABLE : KAMA_RMLUI_CMD_CLIP_DISABLE, 0, Rml::Vector2f(0, 0), 0);
    }

    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                          Rml::Vector2f translation) override
    {
        int32_t kind = KAMA_RMLUI_CMD_CLIP_SET;
        if (operation == Rml::ClipMaskOperation::SetInverse) { kind = KAMA_RMLUI_CMD_CLIP_SET_INVERSE; }
        if (operation == Rml::ClipMaskOperation::Intersect) { kind = KAMA_RMLUI_CMD_CLIP_INTERSECT; }
        record(kind, geometry, translation, 0);
    }

    void SetTransform(const Rml::Matrix4f* transform) override
    {
        if (transform == nullptr || g_recording == nullptr) {
            g_transform = -1;
            return;
        }
        Matrix16 m;
        std::memcpy(m.m, transform->data(), sizeof m.m);
        g_recording->transforms.push_back(m);
        g_transform = static_cast<int32_t>(g_recording->transforms.size()) - 1;
    }

private:
    static size_t addTexture(Texture t)
    {
        if (!g_free_textures.empty()) {
            const size_t slot = g_free_textures.back();
            g_free_textures.pop_back();
            // Carry the slot's generation forward: a renderer keys on it, so it must never repeat for that slot.
            t.generation = g_textures[slot].generation + 1;
            g_textures[slot] = std::move(t);
            return slot + 1;
        }
        t.generation = 1;
        g_textures.push_back(std::move(t));
        return g_textures.size();
    }

    static void record(int32_t kind, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, int32_t texture)
    {
        if (g_recording == nullptr) { return; }
        Command c;
        c.kind = kind;
        if (geometry != 0 && geometry <= g_geometry.size()) {
            c.geometry = static_cast<size_t>(geometry) - 1;
        } else if (kind == KAMA_RMLUI_CMD_DRAW || kind >= KAMA_RMLUI_CMD_CLIP_SET) {
            return;   // a draw of nothing
        } else {
            c.geometry = SIZE_MAX;
        }
        c.translate_x = translation.x;
        c.translate_y = translation.y;
        c.texture = texture;
        c.scissor_enabled = g_scissor_enabled;
        c.scissor_x = g_scissor_x;
        c.scissor_y = g_scissor_y;
        c.scissor_w = g_scissor_w;
        c.scissor_h = g_scissor_h;
        c.transform = g_transform;
        g_recording->commands.push_back(c);
    }
};

class ShimSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return elapsed; }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
    {
        const char* label = "info";
        int32_t t = KAMA_RMLUI_LOG_INFO;
        switch (type) {
            case Rml::Log::LT_ALWAYS:  label = "always";  t = KAMA_RMLUI_LOG_ALWAYS; break;
            case Rml::Log::LT_ERROR:   label = "error";   t = KAMA_RMLUI_LOG_ERROR; break;
            case Rml::Log::LT_ASSERT:  label = "assert";  t = KAMA_RMLUI_LOG_ASSERT; break;
            case Rml::Log::LT_WARNING: label = "warning"; t = KAMA_RMLUI_LOG_WARNING; break;
            case Rml::Log::LT_DEBUG:   label = "debug";   t = KAMA_RMLUI_LOG_DEBUG; break;
            default: break;
        }
        if (g_log_echo) { std::printf("rmlui %s: %s\n", label, message.c_str()); }
        LogLine l;
        l.type = t;
        l.text = message;
        g_log.push_back(std::move(l));
        while (g_log.size() > 512) { g_log.pop_front(); }
        return true;
    }

    double elapsed = 0.0;
};

// One instance per (element, event, phase): it captures no kama state and calls nothing in kama — it appends to the
// queue `kama_rmlui_poll_event` drains.
class QueueingListener final : public Rml::EventListener {
public:
    int32_t     handle = 0;
    int32_t     element = 0;
    int32_t     context = 0;    // whose queue its events go to
    int32_t     binding = 0;    // the binding it was attached through, or 0
    bool        stop = false;
    bool        capture = false;
    Rml::String event;   // what it was attached for, so `unlisten` can name the same (event, phase) pair

    void ProcessEvent(Rml::Event& event) override
    {
        if (g_quiet_change && event.GetId() == Rml::EventId::Change) { return; }
        ContextRec* c = contextAt(context);
        if (c == nullptr) { return; }
        QueuedEvent q;
        q.listener = handle;
        q.binding = binding;
        q.type = eventTypeOf(event.GetId());
        q.element = element;
        q.target = handleOf(event.GetTargetElement());
        q.name = event.GetType();
        q.params = event.GetParameters();
        c->events.push_back(std::move(q));
        if (stop) { event.StopPropagation(); }
    }

    // RmlUi calls this when the listener is removed AND when its element is destroyed — so the listener retires itself,
    // whichever way it goes, and nobody has to detach listeners in the right order before closing a document.
    void OnDetach(Rml::Element*) override
    {
        g_listeners.remove(handle);
        delete this;
    }
};

RecordingRenderer   g_renderer;
ShimSystemInterface g_system;

const Command* commandAt(int32_t context, int32_t i)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr || i < 0 || static_cast<size_t>(i) >= c->commands.size()) { return nullptr; }
    return &c->commands[static_cast<size_t>(i)];
}

const Geometry* geometryFor(int32_t context, int32_t i)
{
    const Command* c = commandAt(context, i);
    if (c == nullptr || c->geometry >= g_geometry.size()) { return nullptr; }
    const Geometry* g = &g_geometry[c->geometry];
    return g->live ? g : nullptr;
}

const Texture* textureAt(int32_t handle)
{
    if (handle <= 0 || static_cast<size_t>(handle) > g_textures.size()) { return nullptr; }
    return &g_textures[static_cast<size_t>(handle) - 1];
}

Rml::ElementFormControl* controlAt(int32_t element)
{
    return rmlui_dynamic_cast<Rml::ElementFormControl*>(elementAt(element));
}

void gatherText(Rml::Element* e, std::string& out)
{
    if (Rml::ElementText* t = rmlui_dynamic_cast<Rml::ElementText*>(e)) { out += t->GetText(); }
    const int n = e->GetNumChildren();
    for (int i = 0; i < n; ++i) { gatherText(e->GetChild(i), out); }
}

} // namespace

// ---- the library -------------------------------------------------------------------------------------------------------

extern "C" int32_t kama_rmlui_initialise(void)
{
    if (g_refs > 0) {
        ++g_refs;
        return 1;
    }
    Rml::SetSystemInterface(&g_system);
    Rml::SetRenderInterface(&g_renderer);
    if (!Rml::Initialise()) {
        std::printf("kama_rmlui: Rml::Initialise failed\n");
        return 0;
    }
    g_refs = 1;
    return 1;
}

extern "C" void kama_rmlui_shutdown(void)
{
    if (g_refs <= 0) { return; }
    if (--g_refs > 0) { return; }

    g_shutting_down = true;
    Rml::Shutdown();   // destroys every context; listeners retire themselves through OnDetach as elements go

    g_contexts.each([&](int32_t, ContextRec*& r) { delete r; r = nullptr; });
    g_contexts.clear();
    g_elements.clear();
    g_element_ids.clear();
    g_elements_at_sweep = 0;
    g_listeners.clear();
    g_bindings.clear();
    g_binding_ids.clear();
    g_geometry.clear();
    g_free_geometry.clear();
    g_textures.clear();
    g_free_textures.clear();
    g_provided.clear();
    g_font_memory.clear();
    g_last_event = QueuedEvent();
    g_recording = nullptr;
    g_system.elapsed = 0.0;
    g_shutting_down = false;
}

extern "C" int32_t kama_rmlui_is_initialised(void) { return g_refs > 0 ? 1 : 0; }

extern "C" int32_t kama_rmlui_version(void) { return scratch(Rml::GetVersion()); }

extern "C" const uint8_t* kama_rmlui_string_data(void)
{
    return reinterpret_cast<const uint8_t*>(g_scratch.data());
}

extern "C" void kama_rmlui_set_time(double seconds)
{
    if (seconds > g_system.elapsed) { g_system.elapsed = seconds; }
}

extern "C" double kama_rmlui_time(void) { return g_system.elapsed; }

// ---- fonts ---------------------------------------------------------------------------------------------------------------

extern "C" int32_t kama_rmlui_load_font_face(const char* path, int32_t fallback)
{
    // NOT guarded on a context: faces go to the font database Rml::Initialise creates, and the useful order is every
    // face first, then the documents that use them.
    if (g_refs <= 0 || path == nullptr || path[0] == '\0') { return 0; }
    if (!Rml::LoadFontFace(path, fallback != 0)) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "kama_rmlui: could not load font face \"%s\" — text in it will not lay out", path);
        return 0;
    }
    return 1;
}

extern "C" int32_t kama_rmlui_load_font_face_memory(const uint8_t* data, int32_t size, const char* family,
                                                    int32_t weight, int32_t italic, int32_t fallback)
{
    if (g_refs <= 0 || data == nullptr || size <= 0) { return 0; }
    g_font_memory.emplace_back(data, data + size);
    const std::vector<uint8_t>& kept = g_font_memory.back();
    const Rml::Style::FontWeight w = static_cast<Rml::Style::FontWeight>(weight < 0 ? 0 : weight);
    const Rml::Style::FontStyle st = italic != 0 ? Rml::Style::FontStyle::Italic : Rml::Style::FontStyle::Normal;
    const bool ok = Rml::LoadFontFace(Rml::Span<const Rml::byte>(kept.data(), kept.size()),
                                      family != nullptr ? family : "", st, w, fallback != 0);
    if (!ok) {
        g_font_memory.pop_back();
        Rml::Log::Message(Rml::Log::LT_WARNING, "kama_rmlui: could not load a font face from %d bytes in memory", size);
        return 0;
    }
    return 1;
}

// ---- provided images -------------------------------------------------------------------------------------------------------

extern "C" void kama_rmlui_provide_image(const char* name, const uint8_t* rgba, int32_t width, int32_t height)
{
    if (name == nullptr || rgba == nullptr || width <= 0 || height <= 0) {
        std::printf("kama_rmlui: provide_image refused — a null name or buffer, or a non-positive size\n");
        return;
    }
    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    // Replace rather than append, so a second provide under one name cannot leave two entries racing in the scan.
    for (ProvidedImage& img : g_provided) {
        if (img.name == name) {
            img.pixels.assign(rgba, rgba + bytes);
            img.width = width;
            img.height = height;
            return;
        }
    }
    ProvidedImage img;
    img.name = name;
    img.pixels.assign(rgba, rgba + bytes);
    img.width = width;
    img.height = height;
    g_provided.push_back(std::move(img));
}

// ---- the log ---------------------------------------------------------------------------------------------------------------

extern "C" void kama_rmlui_set_log_echo(int32_t on) { g_log_echo = on != 0; }

extern "C" int32_t kama_rmlui_poll_log(int32_t* type)
{
    if (g_log.empty()) {
        if (type != nullptr) { *type = 0; }
        return -1;
    }
    LogLine l = std::move(g_log.front());
    g_log.pop_front();
    if (type != nullptr) { *type = l.type; }
    return scratch(l.text);
}

// ---- contexts ----------------------------------------------------------------------------------------------------------------

extern "C" int32_t kama_rmlui_context_create(const char* name, int32_t width, int32_t height)
{
    if (g_refs <= 0) { return 0; }
    std::string n = (name != nullptr) ? name : "";
    if (n.empty()) { n = "kama_rmlui_" + std::to_string(++g_context_counter); }
    Rml::Context* c = Rml::CreateContext(n, Rml::Vector2i(width, height));
    if (c == nullptr) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "kama_rmlui: could not create context \"%s\" (is the name taken?)", n.c_str());
        return 0;
    }
    ContextRec* rec = new ContextRec();
    rec->context = c;
    rec->name = n;
    const int32_t h = g_contexts.add(rec);
    if (h == 0) {
        Rml::RemoveContext(n);
        delete rec;
    }
    return h;
}

extern "C" void kama_rmlui_context_destroy(int32_t context)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr) { return; }
    if (g_recording == c) { g_recording = nullptr; }
    Rml::RemoveContext(c->name);
    g_contexts.remove(context);
    delete c;
}

extern "C" int32_t kama_rmlui_context_is_live(int32_t context) { return contextAt(context) != nullptr ? 1 : 0; }

extern "C" void kama_rmlui_context_set_dimensions(int32_t context, int32_t width, int32_t height)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr) { return; }
    c->context->SetDimensions(Rml::Vector2i(width, height));
}

extern "C" void kama_rmlui_context_set_dp_ratio(int32_t context, float ratio)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr || !(ratio > 0.0f)) { return; }
    c->context->SetDensityIndependentPixelRatio(ratio);
}

extern "C" void kama_rmlui_context_update(int32_t context)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr) { return; }
    c->context->Update();
}

extern "C" void kama_rmlui_context_render(int32_t context)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr) { return; }
    // The command list describes ONE frame, so it is rebuilt from empty; geometry and textures are not.
    c->commands.clear();
    c->transforms.clear();
    g_recording = c;
    g_scissor_enabled = false;
    g_transform = -1;
    c->context->Render();
    g_recording = nullptr;
}

extern "C" int32_t kama_rmlui_context_focus_element(int32_t context)
{
    ContextRec* c = contextAt(context);
    return (c == nullptr) ? 0 : handleOf(c->context->GetFocusElement());
}

extern "C" int32_t kama_rmlui_context_hover_element(int32_t context)
{
    ContextRec* c = contextAt(context);
    return (c == nullptr) ? 0 : handleOf(c->context->GetHoverElement());
}

extern "C" int32_t kama_rmlui_context_element_at_point(int32_t context, float x, float y)
{
    ContextRec* c = contextAt(context);
    return (c == nullptr) ? 0 : handleOf(c->context->GetElementAtPoint(Rml::Vector2f(x, y)));
}

// ---- input in ------------------------------------------------------------------------------------------------------------------

extern "C" int32_t kama_rmlui_context_mouse_move(int32_t context, float x, float y, int32_t mods)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr) { return 0; }
    return c->context->ProcessMouseMove(static_cast<int>(x), static_cast<int>(y), mods) ? 0 : 1;
}

extern "C" int32_t kama_rmlui_context_mouse_button(int32_t context, int32_t button, int32_t down, int32_t mods)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr) { return 0; }
    const bool propagate = (down != 0) ? c->context->ProcessMouseButtonDown(button, mods)
                                       : c->context->ProcessMouseButtonUp(button, mods);
    return propagate ? 0 : 1;
}

extern "C" int32_t kama_rmlui_context_mouse_wheel(int32_t context, float dx, float dy, int32_t mods)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr) { return 0; }
    return c->context->ProcessMouseWheel(Rml::Vector2f(dx, dy), mods) ? 0 : 1;
}

extern "C" int32_t kama_rmlui_context_mouse_leave(int32_t context)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr) { return 0; }
    return c->context->ProcessMouseLeave() ? 0 : 1;
}

extern "C" int32_t kama_rmlui_context_key(int32_t context, int32_t key, int32_t down, int32_t mods)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr || key <= 0 || key > 255) { return 0; }
    const Rml::Input::KeyIdentifier id = static_cast<Rml::Input::KeyIdentifier>(key);
    const bool propagate = (down != 0) ? c->context->ProcessKeyDown(id, mods) : c->context->ProcessKeyUp(id, mods);
    return propagate ? 0 : 1;
}

extern "C" int32_t kama_rmlui_context_text(int32_t context, uint32_t codepoint)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr) { return 0; }
    if (codepoint < 32u || codepoint == 127u || codepoint > 0x10FFFFu) { return 0; }
    return c->context->ProcessTextInput(static_cast<Rml::Character>(codepoint)) ? 0 : 1;
}

extern "C" int32_t kama_rmlui_context_text_utf8(int32_t context, const char* text)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr || text == nullptr || text[0] == '\0') { return 0; }
    return c->context->ProcessTextInput(Rml::String(text)) ? 0 : 1;
}

// ---- documents -------------------------------------------------------------------------------------------------------------------

extern "C" int32_t kama_rmlui_document_load(int32_t context, const char* path)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr || path == nullptr) { return 0; }
    Rml::ElementDocument* d = c->context->LoadDocument(path);
    if (d == nullptr) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "kama_rmlui: could not load document \"%s\"", path);
        return 0;
    }
    return handleOf(d);
}

extern "C" int32_t kama_rmlui_document_load_rml(int32_t context, const char* rml, const char* source_url)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr || rml == nullptr) { return 0; }
    const Rml::String url = (source_url != nullptr && source_url[0] != '\0') ? source_url : "[document from memory]";
    Rml::ElementDocument* d = c->context->LoadDocumentFromMemory(rml, url);
    if (d == nullptr) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "kama_rmlui: could not load document from RML \"%s\"", url.c_str());
        return 0;
    }
    return handleOf(d);
}

extern "C" void kama_rmlui_document_show(int32_t document, int32_t modal, int32_t focus)
{
    Rml::ElementDocument* d = documentAt(document);
    if (d == nullptr) { return; }
    const Rml::ModalFlag m = (modal == 1) ? Rml::ModalFlag::Modal : (modal == 2) ? Rml::ModalFlag::Keep : Rml::ModalFlag::None;
    Rml::FocusFlag f = Rml::FocusFlag::Auto;
    if (focus == 0) { f = Rml::FocusFlag::None; }
    if (focus == 1) { f = Rml::FocusFlag::Document; }
    if (focus == 2) { f = Rml::FocusFlag::Keep; }
    d->Show(m, f);
}

extern "C" void kama_rmlui_document_hide(int32_t document)
{
    Rml::ElementDocument* d = documentAt(document);
    if (d != nullptr) { d->Hide(); }
}

extern "C" void kama_rmlui_document_close(int32_t document)
{
    Rml::ElementDocument* d = documentAt(document);
    if (d != nullptr) { d->Close(); }
}

extern "C" int32_t kama_rmlui_document_title(int32_t document)
{
    Rml::ElementDocument* d = documentAt(document);
    return (d == nullptr) ? -1 : scratch(d->GetTitle());
}

extern "C" int32_t kama_rmlui_document_context(int32_t document)
{
    Rml::ElementDocument* d = documentAt(document);
    return (d == nullptr) ? 0 : contextHandleOf(d->GetContext());
}

extern "C" int32_t kama_rmlui_document_next_tab(int32_t document, int32_t from, int32_t forward, int32_t wrap)
{
    Rml::ElementDocument* d = documentAt(document);
    if (d == nullptr) { return 0; }
    Rml::Element* start = elementAt(from);
    if (start == nullptr) { start = d; }
    Rml::Element* next = d->FindNextTabElement(start, forward != 0, wrap != 0);
    if (next == nullptr || next == static_cast<Rml::Element*>(d)) { return 0; }
    return handleOf(next);
}

// ---- elements --------------------------------------------------------------------------------------------------------------------

extern "C" int32_t kama_rmlui_element_is_live(int32_t element) { return elementAt(element) != nullptr ? 1 : 0; }

extern "C" int32_t kama_rmlui_element_by_id(int32_t scope, const char* id)
{
    Rml::Element* s = elementAt(scope);
    if (s == nullptr || id == nullptr) { return 0; }
    return handleOf(s->GetElementById(id));
}

extern "C" int32_t kama_rmlui_element_query(int32_t scope, const char* selector)
{
    Rml::Element* s = elementAt(scope);
    if (s == nullptr || selector == nullptr) { return 0; }
    return handleOf(s->QuerySelector(selector));
}

extern "C" int32_t kama_rmlui_element_owner_document(int32_t element)
{
    Rml::Element* e = elementAt(element);
    return (e == nullptr) ? 0 : handleOf(e->GetOwnerDocument());
}

extern "C" int32_t kama_rmlui_element_parent(int32_t element)
{
    Rml::Element* e = elementAt(element);
    return (e == nullptr) ? 0 : handleOf(e->GetParentNode());
}

extern "C" int32_t kama_rmlui_element_child_count(int32_t element)
{
    Rml::Element* e = elementAt(element);
    return (e == nullptr) ? 0 : e->GetNumChildren();
}

extern "C" int32_t kama_rmlui_element_child(int32_t element, int32_t index)
{
    Rml::Element* e = elementAt(element);
    if (e == nullptr || index < 0 || index >= e->GetNumChildren()) { return 0; }
    return handleOf(e->GetChild(index));
}

extern "C" int32_t kama_rmlui_element_focus_leaf(int32_t element)
{
    Rml::Element* e = elementAt(element);
    return (e == nullptr) ? 0 : handleOf(e->GetFocusLeafNode());
}

extern "C" int32_t kama_rmlui_element_tag(int32_t element)
{
    Rml::Element* e = elementAt(element);
    return (e == nullptr) ? -1 : scratch(e->GetTagName());
}

extern "C" int32_t kama_rmlui_element_id(int32_t element)
{
    Rml::Element* e = elementAt(element);
    return (e == nullptr) ? -1 : scratch(e->GetId());
}

extern "C" int32_t kama_rmlui_element_inner_rml(int32_t element)
{
    Rml::Element* e = elementAt(element);
    return (e == nullptr) ? -1 : scratch(e->GetInnerRML());
}

extern "C" int32_t kama_rmlui_element_text(int32_t element)
{
    Rml::Element* e = elementAt(element);
    if (e == nullptr) { return -1; }
    std::string out;
    gatherText(e, out);
    return scratch(out);
}

extern "C" int32_t kama_rmlui_element_attribute(int32_t element, const char* name)
{
    Rml::Element* e = elementAt(element);
    if (e == nullptr || name == nullptr) { return -1; }
    const Rml::Variant* v = e->GetAttribute(name);
    if (v == nullptr) { return -1; }
    return scratch(v->Get<Rml::String>());
}

extern "C" int32_t kama_rmlui_element_property(int32_t element, const char* name)
{
    Rml::Element* e = elementAt(element);
    if (e == nullptr || name == nullptr) { return -1; }
    const Rml::Property* p = e->GetProperty(name);
    if (p == nullptr) { return -1; }
    return scratch(p->ToString());
}

extern "C" int32_t kama_rmlui_element_value(int32_t element)
{
    // ⚠️ A DYNAMIC CAST, because "is this a form control" is not answerable from the tag: any element can be asked,
    // and `GetValue` is pure virtual on ElementFormControl — reaching it through anything else would be undefined.
    Rml::ElementFormControl* c = controlAt(element);
    return (c == nullptr) ? -1 : scratch(c->GetValue());
}

extern "C" void kama_rmlui_element_set_inner_rml(int32_t element, const char* rml)
{
    Rml::Element* e = elementAt(element);
    if (e == nullptr || rml == nullptr) { return; }
    e->SetInnerRML(rml);
}

extern "C" void kama_rmlui_element_set_text(int32_t element, const char* text)
{
    Rml::Element* e = elementAt(element);
    if (e == nullptr || text == nullptr) { return; }
    e->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
}

extern "C" void kama_rmlui_element_set_attribute(int32_t element, const char* name, const char* value)
{
    Rml::Element* e = elementAt(element);
    if (e == nullptr || name == nullptr || value == nullptr) { return; }
    e->SetAttribute(name, Rml::String(value));
}

extern "C" void kama_rmlui_element_remove_attribute(int32_t element, const char* name)
{
    Rml::Element* e = elementAt(element);
    if (e == nullptr || name == nullptr) { return; }
    e->RemoveAttribute(name);
}

extern "C" int32_t kama_rmlui_element_has_class(int32_t element, const char* name)
{
    Rml::Element* e = elementAt(element);
    if (e == nullptr || name == nullptr) { return 0; }
    return e->IsClassSet(name) ? 1 : 0;
}

extern "C" void kama_rmlui_element_set_class(int32_t element, const char* name, int32_t on)
{
    Rml::Element* e = elementAt(element);
    if (e == nullptr || name == nullptr) { return; }
    e->SetClass(name, on != 0);
}

extern "C" int32_t kama_rmlui_element_set_property(int32_t element, const char* name, const char* value)
{
    Rml::Element* e = elementAt(element);
    if (e == nullptr || name == nullptr) { return 0; }
    if (value == nullptr || value[0] == '\0') {
        e->RemoveProperty(name);
        return 1;
    }
    return e->SetProperty(name, value) ? 1 : 0;
}

extern "C" int32_t kama_rmlui_element_set_value(int32_t element, const char* value, int32_t quiet)
{
    Rml::ElementFormControl* c = controlAt(element);
    if (c == nullptr || value == nullptr) { return 0; }
    const bool was = g_quiet_change;
    if (quiet != 0) { g_quiet_change = true; }
    c->SetValue(value);
    g_quiet_change = was;
    return 1;
}

extern "C" int32_t kama_rmlui_element_rect(int32_t element, int32_t area, float* x, float* y, float* width, float* height)
{
    Rml::Element* e = elementAt(element);
    Rml::Vector2f offset(0.0f, 0.0f);
    Rml::Vector2f size(0.0f, 0.0f);
    if (e != nullptr) {
        const Rml::BoxArea a = (area >= 0 && area <= 3) ? static_cast<Rml::BoxArea>(area) : Rml::BoxArea::Border;
        offset = e->GetAbsoluteOffset(a);
        size = e->GetBox().GetSize(a);
    }
    // Written on every path, so a caller never reads a stale stack slot.
    if (x != nullptr)      { *x = offset.x; }
    if (y != nullptr)      { *y = offset.y; }
    if (width != nullptr)  { *width = size.x; }
    if (height != nullptr) { *height = size.y; }
    return (e != nullptr) ? 1 : 0;
}

extern "C" int32_t kama_rmlui_element_is_visible(int32_t element)
{
    Rml::Element* e = elementAt(element);
    return (e != nullptr && e->IsVisible(true)) ? 1 : 0;
}

extern "C" int32_t kama_rmlui_element_focus(int32_t element, int32_t visible)
{
    Rml::Element* e = elementAt(element);
    return (e != nullptr && e->Focus(visible != 0)) ? 1 : 0;
}

extern "C" void kama_rmlui_element_blur(int32_t element)
{
    Rml::Element* e = elementAt(element);
    if (e != nullptr) { e->Blur(); }
}

extern "C" void kama_rmlui_element_click(int32_t element)
{
    Rml::Element* e = elementAt(element);
    if (e != nullptr) { e->Click(); }
}

extern "C" void kama_rmlui_element_scroll_into_view(int32_t element)
{
    Rml::Element* e = elementAt(element);
    if (e != nullptr) { e->ScrollIntoView(true); }
}

extern "C" int32_t kama_rmlui_encode_rml(const char* text)
{
    return scratch(Rml::StringUtilities::EncodeRml(text != nullptr ? text : ""));
}

// ---- events out ------------------------------------------------------------------------------------------------------------------------

namespace {

// Attach one queueing listener; its handle, or 0. The one place listeners are made, for plain listens and bindings.
int32_t attachListener(Rml::Element* e, int32_t element, const Rml::String& event, int32_t flags, int32_t binding)
{
    const int32_t context = contextHandleOf(e->GetContext());
    if (context == 0) { return 0; }
    QueueingListener* l = new QueueingListener();
    const int32_t h = g_listeners.add(l);
    if (h == 0) {
        delete l;
        return 0;
    }
    l->handle = h;
    l->element = element;
    l->context = context;
    l->binding = binding;
    l->stop = (flags & KAMA_RMLUI_LISTEN_STOP) != 0;
    l->capture = (flags & KAMA_RMLUI_LISTEN_CAPTURE) != 0;
    l->event = event;
    e->AddEventListener(l->event, l, l->capture);
    return h;
}

std::string bindingKey(int32_t document, const char* id) { return std::to_string(document) + ":" + id; }

// Forget every binding whose document is gone. Bindings are few (a screen's named controls), so a full pass each time a
// new one is minted keeps the table the size of the live screens.
void sweepBindings()
{
    std::vector<int32_t> dead;
    g_bindings.each([&](int32_t h, BindingRec& b) {
        if (documentAt(b.document) == nullptr) { dead.push_back(h); }
    });
    for (int32_t h : dead) {
        BindingRec* b = g_bindings.get(h);
        if (b == nullptr) { continue; }
        g_binding_ids.erase(std::to_string(b->document) + ":" + b->id);
        g_bindings.remove(h);
    }
}

// The binding's element, re-finding it by id when the one it found last is gone — and re-attaching its listeners there.
// nullptr when the document is gone (the binding then dies) or no element has the id right now.
Rml::Element* resolveBinding(int32_t binding)
{
    BindingRec* b = g_bindings.get(binding);
    if (b == nullptr) { return nullptr; }
    Rml::ElementDocument* d = documentAt(b->document);
    if (d == nullptr) {
        g_binding_ids.erase(std::to_string(b->document) + ":" + b->id);
        g_bindings.remove(binding);
        return nullptr;
    }
    Rml::Element* e = elementAt(b->element);
    if (e != nullptr) { return e; }
    e = d->GetElementById(b->id);
    if (e == nullptr) {
        b->element = 0;
        return nullptr;
    }
    b->element = handleOf(e);
    // The listeners on the destroyed element went with it (OnDetach); these are their replacements.
    for (const ListenSpec& spec : b->listens) { attachListener(e, b->element, spec.event, spec.flags, binding); }
    return e;
}

} // namespace

extern "C" int32_t kama_rmlui_listen(int32_t element, const char* event, int32_t flags)
{
    Rml::Element* e = elementAt(element);
    if (e == nullptr || event == nullptr || event[0] == '\0') { return 0; }
    return attachListener(e, element, event, flags, 0);
}

extern "C" void kama_rmlui_unlisten(int32_t listener)
{
    QueueingListener** lp = g_listeners.get(listener);
    if (lp == nullptr || *lp == nullptr) { return; }
    QueueingListener* l = *lp;
    Rml::Element* e = elementAt(l->element);
    if (e == nullptr) {
        // The element is gone; RmlUi detached the listener as it went (OnDetach), so there is nothing left to reach.
        g_listeners.remove(listener);
        return;
    }
    // OnDetach, called from here, deletes `l` and releases the handle.
    e->RemoveEventListener(l->event, l, l->capture);
}

extern "C" int32_t kama_rmlui_poll_event(int32_t context, int32_t* listener, int32_t* type, int32_t* element,
                                         int32_t* target, int32_t* binding)
{
    ContextRec* c = contextAt(context);
    if (c == nullptr || c->events.empty()) {
        g_last_event = QueuedEvent();
        if (listener != nullptr) { *listener = 0; }
        if (type != nullptr)     { *type = 0; }
        if (element != nullptr)  { *element = 0; }
        if (target != nullptr)   { *target = 0; }
        if (binding != nullptr)  { *binding = 0; }
        return 0;
    }
    g_last_event = std::move(c->events.front());
    c->events.pop_front();
    if (listener != nullptr) { *listener = g_last_event.listener; }
    if (type != nullptr)     { *type = g_last_event.type; }
    if (element != nullptr)  { *element = g_last_event.element; }
    if (target != nullptr)   { *target = g_last_event.target; }
    if (binding != nullptr)  { *binding = g_last_event.binding; }
    return 1;
}

extern "C" void kama_rmlui_clear_events(int32_t context)
{
    ContextRec* c = contextAt(context);
    if (c != nullptr) { c->events.clear(); }
}

// ---- bindings ------------------------------------------------------------------------------------------------------------------------------

extern "C" int32_t kama_rmlui_bind(int32_t document, const char* id)
{
    Rml::ElementDocument* d = documentAt(document);
    if (d == nullptr || id == nullptr || id[0] == '\0') { return 0; }
    const std::string key = bindingKey(document, id);
    auto it = g_binding_ids.find(key);
    if (it != g_binding_ids.end()) {
        if (g_bindings.get(it->second) != nullptr) {
            resolveBinding(it->second);   // binding again is "touch it": re-find and re-listen after a rewrite
            return it->second;
        }
        g_binding_ids.erase(it);
    }
    Rml::Element* e = d->GetElementById(id);
    if (e == nullptr) { return 0; }
    sweepBindings();
    BindingRec b;
    b.document = document;
    b.id = id;
    b.element = handleOf(e);
    const int32_t h = g_bindings.add(b);
    if (h != 0) { g_binding_ids[key] = h; }
    return h;
}

extern "C" int32_t kama_rmlui_binding_element(int32_t binding)
{
    Rml::Element* e = resolveBinding(binding);
    return (e == nullptr) ? 0 : handleOf(e);
}

extern "C" int32_t kama_rmlui_binding_listen(int32_t binding, const char* event, int32_t flags)
{
    if (event == nullptr || event[0] == '\0') { return 0; }
    Rml::Element* e = resolveBinding(binding);
    BindingRec* b = g_bindings.get(binding);
    if (b == nullptr) { return 0; }
    for (const ListenSpec& spec : b->listens) {
        if (spec.event == event && spec.flags == flags) { return 1; }
    }
    ListenSpec spec;
    spec.event = event;
    spec.flags = flags;
    b->listens.push_back(spec);
    if (e != nullptr) { attachListener(e, b->element, spec.event, spec.flags, binding); }
    return 1;
}

extern "C" int32_t kama_rmlui_binding_of(int32_t document, int32_t element)
{
    if (element == 0 || documentAt(document) == nullptr) { return 0; }
    std::vector<int32_t> mine;
    g_bindings.each([&](int32_t h, BindingRec& b) {
        if (b.document == document) { mine.push_back(h); }
    });
    for (int32_t h : mine) {
        Rml::Element* e = resolveBinding(h);
        if (e != nullptr && handleOf(e) == element) { return h; }
    }
    return 0;
}

extern "C" int32_t kama_rmlui_binding_id(int32_t binding)
{
    BindingRec* b = g_bindings.get(binding);
    return (b == nullptr) ? -1 : scratch(b->id);
}

extern "C" int32_t kama_rmlui_event_name(void) { return scratch(g_last_event.name); }

extern "C" int32_t kama_rmlui_event_param_int(const char* name, int32_t fallback)
{
    if (name == nullptr) { return fallback; }
    auto it = g_last_event.params.find(name);
    if (it == g_last_event.params.end()) { return fallback; }
    return it->second.Get<int>(fallback);
}

extern "C" float kama_rmlui_event_param_float(const char* name, float fallback)
{
    if (name == nullptr) { return fallback; }
    auto it = g_last_event.params.find(name);
    if (it == g_last_event.params.end()) { return fallback; }
    return it->second.Get<float>(fallback);
}

extern "C" int32_t kama_rmlui_event_param_string(const char* name)
{
    if (name == nullptr) { return -1; }
    auto it = g_last_event.params.find(name);
    if (it == g_last_event.params.end()) { return -1; }
    return scratch(it->second.Get<Rml::String>());
}

// ---- reading back the recorded draw commands ----------------------------------------------------------------------------------------------

extern "C" int32_t kama_rmlui_command_count(int32_t context)
{
    ContextRec* c = contextAt(context);
    return (c == nullptr) ? 0 : static_cast<int32_t>(c->commands.size());
}

extern "C" int32_t kama_rmlui_command_kind(int32_t context, int32_t i)
{
    const Command* c = commandAt(context, i);
    return (c == nullptr) ? 0 : c->kind;
}

extern "C" const float* kama_rmlui_command_vertices(int32_t context, int32_t i)
{
    const Geometry* g = geometryFor(context, i);
    return (g == nullptr || g->vertices.empty()) ? nullptr : g->vertices.data();
}

extern "C" int32_t kama_rmlui_command_vertex_count(int32_t context, int32_t i)
{
    const Geometry* g = geometryFor(context, i);
    return (g == nullptr) ? 0 : static_cast<int32_t>(g->vertices.size() / 8);
}

extern "C" const int32_t* kama_rmlui_command_indices(int32_t context, int32_t i)
{
    const Geometry* g = geometryFor(context, i);
    return (g == nullptr || g->indices.empty()) ? nullptr : g->indices.data();
}

extern "C" int32_t kama_rmlui_command_index_count(int32_t context, int32_t i)
{
    const Geometry* g = geometryFor(context, i);
    return (g == nullptr) ? 0 : static_cast<int32_t>(g->indices.size());
}

extern "C" int32_t kama_rmlui_command_geometry(int32_t context, int32_t i)
{
    const Geometry* g = geometryFor(context, i);
    return (g == nullptr) ? 0 : g->id;
}

extern "C" int32_t kama_rmlui_command_texture(int32_t context, int32_t i)
{
    const Command* c = commandAt(context, i);
    return (c == nullptr) ? 0 : c->texture;
}

extern "C" float kama_rmlui_command_translate_x(int32_t context, int32_t i)
{
    const Command* c = commandAt(context, i);
    return (c == nullptr) ? 0.0f : c->translate_x;
}

extern "C" float kama_rmlui_command_translate_y(int32_t context, int32_t i)
{
    const Command* c = commandAt(context, i);
    return (c == nullptr) ? 0.0f : c->translate_y;
}

extern "C" int32_t kama_rmlui_command_scissor(int32_t context, int32_t i, int32_t* x, int32_t* y, int32_t* width,
                                              int32_t* height)
{
    const Command* c = commandAt(context, i);
    const bool on = (c != nullptr) && c->scissor_enabled;
    if (x != nullptr)      { *x = on ? c->scissor_x : 0; }
    if (y != nullptr)      { *y = on ? c->scissor_y : 0; }
    if (width != nullptr)  { *width = on ? c->scissor_w : 0; }
    if (height != nullptr) { *height = on ? c->scissor_h : 0; }
    return on ? 1 : 0;
}

extern "C" int32_t kama_rmlui_command_transform(int32_t context, int32_t i, float* out16)
{
    const Command* c = commandAt(context, i);
    ContextRec* rec = contextAt(context);
    const bool on = (c != nullptr) && (rec != nullptr) && c->transform >= 0 &&
                    static_cast<size_t>(c->transform) < rec->transforms.size();
    if (out16 != nullptr) {
        if (on) {
            std::memcpy(out16, rec->transforms[static_cast<size_t>(c->transform)].m, sizeof(float) * 16);
        } else {
            for (int k = 0; k < 16; ++k) { out16[k] = (k % 5 == 0) ? 1.0f : 0.0f; }
        }
    }
    return on ? 1 : 0;
}

// ---- textures ----------------------------------------------------------------------------------------------------------------------------

extern "C" int32_t kama_rmlui_texture_count(void) { return static_cast<int32_t>(g_textures.size()); }

extern "C" int32_t kama_rmlui_texture_width(int32_t texture)
{
    const Texture* t = textureAt(texture);
    return (t == nullptr) ? 0 : t->width;
}

extern "C" int32_t kama_rmlui_texture_height(int32_t texture)
{
    const Texture* t = textureAt(texture);
    return (t == nullptr) ? 0 : t->height;
}

extern "C" const uint8_t* kama_rmlui_texture_data(int32_t texture)
{
    const Texture* t = textureAt(texture);
    return (t == nullptr || t->pixels.empty()) ? nullptr : t->pixels.data();
}

extern "C" int32_t kama_rmlui_texture_generation(int32_t texture)
{
    const Texture* t = textureAt(texture);
    return (t == nullptr) ? 0 : t->generation;
}
