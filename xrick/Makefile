# Makefile — added 2026-09-04 for `review-plan.md` R0.1.
#
# The tree shipped with an MSVC project only (xrick.vcxproj), which expects
# ..\..\lib\SDL2-2.0.9 and ..\..\lib\zlib1211 — neither present in the repo. This
# builds the same sources against the system SDL3 (T20) so the review can compile,
# warn and run on Linux/WSL.
#
# Targets (output under ../build/wsl, see OUTDIR):
#   make            release-ish build with the full warning set -> ../build/wsl/xrick
#   make warn       syntax-only pass over the game logic (the R0.1 evidence)
#   make clean      removes ../build/wsl

CC      ?= gcc
CXX     ?= g++
# Senjin's SDL3 port: src/sysvid_gpu.c, an SDL_GPU renderer (Vulkan/D3D12/
# Metal) replacing src/sysvid_gl.c's OpenGL chain for this native Linux
# build -- see sysvid_gpu.c's own header comment for why both files exist
# (the web build, via build.sh, keeps using sysvid_gl.c/WebGL). Needs
# glslc (shaderc or the Vulkan SDK) to compile its shaders to SPIR-V.
GLSLC   ?= glslc
# T20: upgraded from SDL2 to SDL3 -- see build.md. SDL3 dropped sdl2-config-style
# shell scripts in favor of pkg-config exclusively.
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS   := $(shell pkg-config --libs sdl3)

# The warning set is deliberate: -Wconversion/-Wsign-conversion are how phase 2
# (variable widths) gets its candidate list. Do not silence them.
WARN    := -Wall -Wextra -Wconversion -Wsign-conversion -Wtype-limits
# -fcommon: this is pre-C99-style code with tentative definitions of the same symbol in
# several translation units (WAV_*). MSVC and pre-10 GCC allowed it; modern
# GCC defaults to -fno-common and rejects it. Build portability only.
# PLATFORM=ST (default) or PLATFORM=PC -- selects game behaviour and data.
PLATFORM ?= ST
# RD1/RD2 split (2026-09-22): include/ and src/ hold platform-common code; each game's
# own engine and data live in include/rd1|rd2 and src/rd1|rd2. -Isrc(/rd1|/rd2) is needed
# because a couple of common files quote-include a game's own subtree (e.g. sysvid.c's
# "img_icon.e", syssnd.c's "audio_engine/AtariMachineC.h") and GCC's quote-include only
# checks the including file's own directory before falling back to -I.
INC := -Iinclude -Iinclude/rd1 -Iinclude/rd2 -Isrc -Isrc/rd1 -Isrc/rd2
CFLAGS  += $(INC) $(SDL_CFLAGS) $(WARN) -fcommon -O2 -g -DPLATFORM_$(PLATFORM)
# T19 / audio-sndh.md S9: src/audio_engine/ is AtariAudio, vendored verbatim from
# arnaud-carre/AtariAudio (MIT) -- C++, not C. Built with its own, more lenient flags;
# this tree's -Wconversion/-Wsign-conversion discipline is not held against vendored
# third-party code. Quote-includes resolve same-directory (ym2149c.h, external/Musashi/
# m68k.h, etc.), so no extra -I is needed beyond what CFLAGS already has.
CXXFLAGS += $(INC) $(SDL_CFLAGS) -O2 -g -std=c++17
# T22: zlib/unzip dropped -- see build.md. Nothing loads assets from disk anymore;
# every WAV_*/dat_* table is compiled in, and data_file_{open,read,close,...} (the
# only things that ever called into zlib) were dead code with zero callers.
LDFLAGS +=
LIBS    := $(SDL_LIBS) -lm

CSRC := $(wildcard src/*.c) $(wildcard src/rd1/*.c) $(wildcard src/rd2/*.c)

# GFXST is selected in config.h, so the GFXPC data tables must not be compiled.
# sysvid_gl.c is excluded here: this native build uses sysvid_gpu.c instead (see
# the GLSLC comment above) -- both define the same sysvid_gl_* symbols sysvid.c
# calls, so including both would be a duplicate-symbol link error.
CSRC := $(filter-out src/rd1/dat_picsPC.c src/rd1/dat_spritesPC.c src/rd1/dat_tilesPC.c src/sysvid_gl.c,$(CSRC))

CXXSRC := $(wildcard src/audio_engine/*.cpp) $(wildcard src/audio_engine/external/Musashi/*.cpp)

# sysvid_gpu.c's shaders: compiled from GLSL to SPIR-V and #embed'd into
# shaders_embedded.c (needs a C23 compiler; see its target-specific CFLAGS
# override below). Output under the repo's build/ (shaders_embedded.c's
# #embed paths are relative to it), separate from this build's own OUTDIR.
SHADER_DIR   := src/shaders/gpu
SHADER_BUILD := ../build/shaders
SHADER_SRCS  := $(wildcard $(SHADER_DIR)/*.vert $(SHADER_DIR)/*.frag)
SHADER_SPVS  := $(patsubst $(SHADER_DIR)/%,$(SHADER_BUILD)/%.spv,$(SHADER_SRCS))

# All build output goes under the repo's build/ (git-ignored, never sources; build.sh
# uses build/win and build/web): the binary is build/wsl/xrick, objects in
# build/wsl/obj/, the `make warn` log build/wsl/warn.log. PLATFORM=ST and PLATFORM=PC
# share this tree, so `make clean` when switching.
OUTDIR := ../build/wsl
OBJ := $(CSRC:%.c=$(OUTDIR)/obj/%.o) $(CXXSRC:%.cpp=$(OUTDIR)/obj/%.o)
BIN := $(OUTDIR)/xrick
WARNLOG := $(OUTDIR)/warn.log

# Game logic only — no generated tables, no SDL layer, no vendored library. This is the
# set that review-plan.md phases 2-3 actually read.
LOGIC := $(filter-out $(wildcard src/rd1/dat_*.c) $(wildcard src/rd2/dat_*.c) $(wildcard src/sys*.c) src/rd1/syssnd.c src/rd2/syssnd.c,$(CSRC))

all: $(BIN)

# Linked with CXX (not CC) because audio_engine/ pulls in libstdc++ -- harmless for the
# C objects, required for the C++ ones.
$(BIN): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

# shaders_embedded.c's #embed directives need the .spv files to exist first, and a
# C23 compiler -- only this one file needs that dialect, so it's a target-specific
# override rather than a blanket -std= for every C file.
$(OUTDIR)/obj/src/shaders_embedded.o: $(SHADER_SPVS)
$(OUTDIR)/obj/src/shaders_embedded.o $(OUTDIR)/obj/src/bezels_embedded.o $(OUTDIR)/obj/src/masks_embedded.o $(OUTDIR)/obj/src/gameselect_embedded.o: CFLAGS += -std=gnu23

$(OUTDIR)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(SHADER_BUILD)/%.vert.spv: $(SHADER_DIR)/%.vert
	@mkdir -p $(dir $@)
	$(GLSLC) -fshader-stage=vert $< -o $@

$(SHADER_BUILD)/%.frag.spv: $(SHADER_DIR)/%.frag
	@mkdir -p $(dir $@)
	$(GLSLC) -fshader-stage=frag $< -o $@

# R0.1 evidence: warnings over the logic surface, without linking.
warn:
	@mkdir -p $(OUTDIR)
	@: > $(WARNLOG)
	@for f in $(LOGIC); do $(CC) -fsyntax-only $(INC) $(SDL_CFLAGS) $(WARN) $$f 2>> $(WARNLOG); done
	@echo "=== warning census ==="
	@grep -oE '\[-W[a-z-]+\]' $(WARNLOG) | sort | uniq -c | sort -rn
	@echo "total: $$(grep -c 'warning:' $(WARNLOG)) warnings, $$(grep -c 'error:' $(WARNLOG)) errors"

clean:
	rm -rf $(OUTDIR) $(SHADER_BUILD)

.PHONY: all warn clean
