# Builds RmlUi 6.3's core and FreeType 2.14.3 into static archives under build/<plat>/lib/, for kama to link.
#
#   make -C native            # -> build/<plat>/lib/librmlui.a + build/<plat>/lib/libfreetype.a
#   make -C native freetype   # FreeType alone (build/<plat>/freetype/lib/libfreetype.a)
#   make -C native rmlui      # RmlUi alone (needs FreeType first)
#   make -C native clean
#
# From the-mallow-man's native/Makefile (its `rmlui` and `freetype` targets), itself from
# friendly-fire-department@a676ca3, with the same switches and the same warnings. Sources come from
# tools/fetch-rmlui.sh and tools/fetch-freetype.sh (pinned by SHA).
#
# WHY CMAKE AND NOT `csources`, which is how @kama/sodium builds libsodium: RmlUi is ~200 C++ files and FreeType's
# build is driven by its CMake configuration (module list, options), so kama would compile all of it into every
# consumer's build at the consumer's optimisation level — a debug `kama build` would link an -O0 layout engine. This
# builds them once, -O2 (Release), whatever the consumer's tier. kama's own position is that a package carries
# sources, not binaries, and that "for what cannot be built that way — a cmake project — the PACKAGE says what it needs
# (`link`) and the ROOT project says where it is (its own `ldflags`)" (docs/targets.md); that is this arrangement.
# The package's own C++ (csrc/kama_rmlui.cpp) IS a `csources` entry: kama compiles it, so kama also links with the
# C++ driver and brings the C++ runtime on every platform.
#
# - **FreeType first, minimal and static**: HarfBuzz, Brotli, BZip2, PNG and system zlib all OFF (RmlUi shapes
#   nothing; plain TrueType/OpenType faces), installed into build/<plat>/freetype for RmlUi's find_package and the
#   link — into its lib/ by name (GNUInstallDirs says lib64 on Fedora-family Linux, where a fixed path would miss).
#   ⚠️ friendly-fire-department measured that `CMAKE_PREFIX_PATH` alone makes RmlUi's FindFreetype take THIS archive
#   over MSYS2's, which sits on the default search path too — check `FREETYPE_LIBRARY_RELEASE` in
#   build/<plat>/rmlui/CMakeCache.txt if a Windows binary ever imports freetype's DLL chain. ⚠️ **An RmlUi configured
#   before FreeType was built caches the platform's FreeType: `rm -rf native/build/<plat>/rmlui` once.**
# - **RmlUi static, no samples, no tests, no precompiled headers**, font engine at its default (FreeType). Target
#   `rmlui_core` only: the debugger and Lua plugins are not built. Exceptions and RTTI stay ON: RmlUi uses both, and
#   the shim uses `rmlui_dynamic_cast`.
# - ⚠️ **`RMLUI_STATIC_LIB` must be defined wherever RmlUi's headers are included against this archive** — on Windows
#   every call is otherwise a `__declspec(dllimport)` and the link fails on `__imp__ZN3Rml…` against a perfectly good
#   librmlui.a (friendly-fire-department). csrc/kama_rmlui.cpp defines it itself, on every platform, because this
#   archive is static on every platform.
# - **C++17**, because RmlUi 6 is. kama compiles C++ `csources` as gnu++17 already.
#
# Host and container may share this tree, so the output is per-platform, named by tools/platform.
UNAME := $(shell uname -s)
PLAT  := $(shell ../tools/platform)
BUILD := build/$(PLAT)

RMLUI_SRC   ?= ../third_party/rmlui
RMLUI_BUILD := $(BUILD)/rmlui
RMLUI_LIB   := $(RMLUI_BUILD)/librmlui.a
RMLUI_PIN   := $(RMLUI_SRC)/.fetch-rmlui-pin
FT_SRC      ?= ../third_party/freetype
FT_BUILD    := $(BUILD)/freetype-build
FT_INSTALL  := $(BUILD)/freetype
FT_LIB      := $(FT_INSTALL)/lib/libfreetype.a
FT_PIN      := $(FT_SRC)/.fetch-freetype-pin

# The one directory a consumer names in its `ldflags` (`-L<this package>/native/build/<plat>/lib`).
LIB_DIR     := $(BUILD)/lib

# The compilers' stamp, a prerequisite of every leg ("the compiler stamp", the end of this file).
TOOLCHAIN   := $(BUILD)/.toolchain

# Resolved with `command -v` rather than left as the bare word: friendly-fire-department's emscripten image has a
# DIRECTORY named cmake ahead of /usr/bin on PATH, which make tries to exec ("Permission denied").
CMAKE ?= $(shell command -v cmake 2>/dev/null || echo cmake)

# ⚠️ **ON WINDOWS THE COMPILERS ARE NAMED, AND THEY ARE GNU, ON PURPOSE.** A consumer that links libstdc++ statically
# links the archive MSYS2's GCC built. friendly-fire-department measured the alternative: C++ compiled by clang++
# against GCC's static libstdc++ printed 35-100 "duplicate section ... has different size" COMDAT warnings per link;
# g++ printed none. So on Windows build kama with `--cxx g++` too (README.md, "Windows"). Ninja because MSYS2's cmake
# has no Unix Makefiles default worth relying on.
ifeq ($(PLAT),windows)
  CMAKE_PLAT := -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
endif

# macOS: build for the oldest macOS a consumer is likely to ship to, and for this machine's arch only.
ifeq ($(UNAME),Darwin)
  MACOS_MIN := $(shell ../tools/platform macos-min)
  export MACOSX_DEPLOYMENT_TARGET := $(MACOS_MIN)
  CMAKE_PLAT := -DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOS_MIN) -DCMAKE_OSX_ARCHITECTURES=$(shell uname -m)
endif

# Absolute paths in asserts and __FILE__ would name the build machine's home directory in a shipped binary
# (friendly-fire-department FR-37). Not on Windows: cmake hands MSYS2's gcc `C:/…` paths, which a `/c/…` prefix map
# would not match. -fPIC elsewhere, so the archive can also go into a shared library (`kama build --shared`).
ifneq ($(PLAT),windows)
  CMAKE_FLAGS := -DCMAKE_C_FLAGS="-fPIC -ffile-prefix-map=$(abspath ..)=." \
                 -DCMAKE_CXX_FLAGS="-fPIC -ffile-prefix-map=$(abspath ..)=."
endif

.PHONY: all rmlui freetype clean
all: $(LIB_DIR)/librmlui.a $(LIB_DIR)/libfreetype.a
rmlui: $(RMLUI_LIB)
freetype: $(FT_LIB)

# Copied, not linked, so the directory is self-contained (a Windows checkout has no symlinks worth trusting).
$(LIB_DIR)/librmlui.a: $(RMLUI_LIB)
	@mkdir -p $(dir $@)
	@cp $< $@
	@echo "native: $@"

$(LIB_DIR)/libfreetype.a: $(FT_LIB)
	@mkdir -p $(dir $@)
	@cp $< $@
	@echo "native: $@"

$(RMLUI_LIB): $(RMLUI_PIN) $(FT_LIB) $(TOOLCHAIN)
	$(CMAKE) -S $(RMLUI_SRC) -B $(RMLUI_BUILD) $(CMAKE_PLAT) $(CMAKE_FLAGS) \
	      -DCMAKE_PREFIX_PATH=$(abspath $(FT_INSTALL)) \
	      -DCMAKE_BUILD_TYPE=Release \
	      -DBUILD_SHARED_LIBS=OFF \
	      -DRMLUI_SAMPLES=OFF \
	      -DBUILD_TESTING=OFF \
	      -DRMLUI_PRECOMPILED_HEADERS=OFF
	$(CMAKE) --build $(RMLUI_BUILD) --target rmlui_core --parallel $(CLEAN_FIRST)
	@test -f $(RMLUI_LIB) || { echo "native: cmake built rmlui_core but no $(RMLUI_LIB)" >&2; exit 1; }
	@touch $(RMLUI_LIB)
	@echo "native: $(RMLUI_LIB)"

$(FT_LIB): $(FT_PIN) $(TOOLCHAIN)
	$(CMAKE) -S $(FT_SRC) -B $(FT_BUILD) $(CMAKE_PLAT) $(CMAKE_FLAGS) \
	      -DCMAKE_BUILD_TYPE=Release \
	      -DCMAKE_INSTALL_PREFIX=$(abspath $(FT_INSTALL)) \
	      -DCMAKE_INSTALL_LIBDIR=lib \
	      -DBUILD_SHARED_LIBS=OFF \
	      -DFT_DISABLE_HARFBUZZ=ON \
	      -DFT_DISABLE_BROTLI=ON \
	      -DFT_DISABLE_BZIP2=ON \
	      -DFT_DISABLE_PNG=ON \
	      -DFT_DISABLE_ZLIB=ON
	$(CMAKE) --build $(FT_BUILD) --parallel $(CLEAN_FIRST)
	$(CMAKE) --install $(FT_BUILD)
	@touch $(FT_LIB)
	@echo "native: $(FT_LIB)"

$(RMLUI_PIN):
	@echo "native: no fetched RmlUi ($@) — run tools/fetch-rmlui.sh" >&2; exit 1
$(FT_PIN):
	@echo "native: no fetched FreeType ($@) — run tools/fetch-freetype.sh" >&2; exit 1

clean:
	rm -rf $(BUILD)

# ---- the compiler stamp -----------------------------------------------------------------------------------------------
#
# ⚠️ **A NEW COMPILER CHANGES WHAT THESE ARCHIVES HOLD, AND make CANNOT SEE ONE — it knows files, not compilers.**
# (the-mallow-man M3a N23: MSYS2's gcc moved a patch release under a built archive and nothing rebuilt it.) So the first
# `--version` line of every compiler cmake builds with goes into build/<plat>/.toolchain, REWRITTEN ONLY WHEN IT
# CHANGED: its mtime is the last compiler change, and a make with nothing to do still does nothing. Both legs depend on
# it, so a changed compiler rebuilds them from clean (CLEAN_FIRST: cmake's build files track sources and headers, never
# the compiler binary).
ifeq ($(PLAT),windows)
  CMAKE_CCS := gcc g++
else
  CMAKE_CCS := $(if $(filter environment%,$(origin CC)),$(CC),cc) $(if $(filter environment%,$(origin CXX)),$(CXX),c++)
endif
TOOLCHAIN_NEW := $(shell mkdir -p $(BUILD); \
  now="$$(for c in $(CMAKE_CCS); do v="$$("$$c" --version 2>/dev/null | head -n 1)"; printf '%s: %s\n' "$$c" "$${v:-(none)}"; done)"; \
  if [ "$$now" != "$$(cat $(TOOLCHAIN) 2>/dev/null)" ]; then printf '%s\n' "$$now" > $(TOOLCHAIN); echo new; fi)
ifneq ($(TOOLCHAIN_NEW),)
  $(info native: $(TOOLCHAIN) — the compilers are new here, so both legs rebuild from clean)
endif
# `=`, not `:=`: `$?` (the prerequisites newer than the target) is the recipe's, read when it runs.
CLEAN_FIRST = $(if $(filter $(TOOLCHAIN),$?),--clean-first)

$(TOOLCHAIN):
	@echo "native: no $(TOOLCHAIN) — the compiler stamp could not be written (is $(BUILD) writable?)" >&2; exit 1
