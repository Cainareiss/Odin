CXX := g++
CC := gcc
LD := g++

SDL_CFLAGS := `pkg-config --cflags sdl3`
SDL_LDFLAGS := `pkg-config --libs sdl3`

PKG_CFLAGS := `pkg-config --cflags gl glew jansson x11`
PKG_LDFLAGS := `pkg-config --libs gl glew jansson x11`

OPT_FLAGS := -O3 -fomit-frame-pointer -flto -march=native -fno-stack-protector
#OPT_FLAGS := -O0 -g
LDFLAGS := -lm $(PKG_LDFLAGS) $(SDL_LDFLAGS)

# Profile-guided build, 4 to 15 % faster: make clean, make PGO=generate, run
# the games for a while, make clean (the profiles stay), make PGO=use.
ifeq ($(PGO),generate)
PGO_FLAGS := -fprofile-generate -fprofile-update=single
endif
ifeq ($(PGO),use)
PGO_FLAGS := -fprofile-use -fprofile-correction -Wno-missing-profile
endif

# Each object notes the headers it was built from, in a .d file next to it.
DEP_FLAGS := -MMD -MP

# A product is fused with a sum only where the code says std::fma: every
# processor, with FMA or without, then computes the same and draws the same
# pictures (MARKDOWN/105 § 11).
FP_FLAGS := -ffp-contract=off

CXXFLAGS := -std=c++20 -pthread -I src -Wall -Wextra -Wpedantic -Wconversion -Wshadow $(PKG_CFLAGS) $(SDL_CFLAGS) $(OPT_FLAGS) $(FP_FLAGS) $(PGO_FLAGS) $(DEP_FLAGS)

.PHONY: all install clean spirv

# The front end, the board, then its chips
OBJ := \
	src/odin/main.o \
	src/odin/cpu_check.o \
	src/odin/options.o \
	src/odin/menu_session.o \
	src/odin/menu_session_input.o \
	src/odin/menu_session_state.o \
	src/odin/session.o \
	src/odin/library.o \
	src/odin/display.o \
	src/odin/display_bezel.o \
	src/odin/display_window.o \
	src/odin/host_graphics_gl.o \
	src/odin/host_graphics_vk.o \
	src/odin/vk_common.o \
	src/odin/board_drawing_vk.o \
	src/odin/board_pictures_vk.o \
	src/odin/board_scene_vk.o \
	src/odin/board_translucent_vk.o \
	src/odin/board_drawing_gl.o \
	src/odin/board_pictures_gl.o \
	src/odin/board_programs_gl.o \
	src/odin/board_scene_gl.o \
	src/odin/board_translucent_gl.o \
	src/odin/x11_window.o \
	src/odin/menu.o \
	src/odin/menu_input.o \
	src/odin/menu_pages.o \
	src/odin/menu_library.o \
	src/odin/menu_bar.o \
	src/odin/cabinet_view.o \
	src/odin/menu_kit.o \
	src/odin/menu_nav.o \
	src/odin/menu_pictures.o \
	src/odin/menu_fonts.o \
	src/odin/system_fonts.o \
	src/odin/rom_files.o \
	src/odin/folder_dialog.o \
	src/odin/pad_view.o \
	src/odin/pad_drawings.o \
	src/odin/pad_drawings_al2009man.o \
	src/odin/keyboard.o \
	src/odin/log_file.o \
	src/odin/version.o \
	src/odin/hotkeys.o \
	src/odin/controls.o \
	src/odin/pointers.o \
	src/odin/wheel_force.o \
	src/odin/evdev.o \
	src/odin/rawinput.o \
	src/odin/outputs_network.o \
	src/odin/sockets.o \
	src/odin/cabinet_settings.o \
	src/odin/control_panel.o \
	src/odin/bindings.o \
	src/odin/settings.o \
	src/odin/games.o \
	src/odin/roles.o \
	src/odin/state_file.o \
	src/odin/user_files.o \
	src/odin/worker.o \
	src/odin/hikaru.o \
	src/odin/hikaru_netbd.o \
	src/odin/hikaru_bus.o \
	src/odin/hikaru_state.o \
	src/odin/scheduler.o \
	src/odin/memctl.o \
	src/odin/rom_board.o \
	src/odin/eeprom_93c46.o \
	src/odin/x76f100.o \
	src/odin/crypt_315_5881.o \
	src/odin/actel_m1.o \
	src/odin/sh4.o \
	src/odin/sh4_fsca.o \
	src/odin/host_code.o \
	src/odin/sh4_host.o \
	src/odin/sh4_translate.o \
	src/odin/sh4_ops.o \
	src/odin/sh4_onchip.o \
	src/odin/sh4_clock.o \
	src/odin/mie_board.o \
	src/odin/mie.o \
	src/odin/z80.o \
	src/odin/z80_ops.o \
	src/odin/jvs.o \
	src/odin/io_837_13551_imitation.o \
	src/odin/io_837_13741_imitation.o \
	src/odin/io_837_13551.o \
	src/odin/tmp90ph44.o \
	src/odin/io_837_13741.o \
	src/odin/d71054.o \
	src/odin/drive_838_13366.o \
	src/odin/netbd_837_13404.o \
	src/odin/netbd_ring.o \
	src/odin/m68000.o \
	src/odin/m68000_ops.o \
	src/odin/tlcs90.o \
	src/odin/card_reader.o \
	src/odin/von4_card.o \
	src/odin/sound_board.o \
	src/odin/aica.o \
	src/odin/aica_dsp.o \
	src/odin/audio_out.o \
	src/odin/aica_voice.o \
	src/odin/wav_file.o \
	src/odin/arm7.o \
	src/odin/arm7_ops.o \
	src/odin/arm7_host.o \
	src/odin/arm7_translate.o \
	src/odin/hikaru_gpu.o \
	src/odin/antarctic.o \
	src/odin/antarctic_ops.o \
	src/odin/antarctic_mesh.o \
	src/odin/antarctic_disasm.o \
	src/odin/antarctic_list.o \
	src/odin/antarctic_io.o \
	src/odin/eurasia.o \
	src/odin/renderer.o \
	src/odin/renderer_scene.o \
	src/odin/renderer_translucent.o \
	src/odin/renderer_programs.o \
	src/odin/program_cache.o \
	src/odin/glsl_texts.o \
	src/odin/spirv.o \
	src/odin/renderer_textures.o \
	src/odin/renderer_times.o \
	src/odin/renderer_layers.o \
	src/odin/renderer_state.o \
	src/odin/renderer_debug.o \
	src/odin/gl_common.o \
	src/odin/picture_filter_gl.o \
	src/odin/picture_filter_vk.o \
	src/odin/picture_preset.o \
	src/odin/loading_screen.o

# Dear ImGui as it comes, for the menu: built without our warnings
IMGUI_OBJ := \
	src/imgui/imgui.o \
	src/imgui/imgui_draw.o \
	src/imgui/imgui_tables.o \
	src/imgui/imgui_widgets.o \
	src/imgui/imgui_impl_sdl3.o \
	src/imgui/imgui_impl_opengl3.o \
	src/imgui/imgui_impl_vulkan.o

# Its Vulkan backend calls the functions Odin loads (vk_common.hpp), not
# those of a linked loader
VULKAN_FLAGS := -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES
IMGUI_FLAGS := -std=c++20 -I src/imgui $(SDL_CFLAGS) $(OPT_FLAGS) $(FP_FLAGS) $(PGO_FLAGS) $(DEP_FLAGS) $(VULKAN_FLAGS)

# miniz and the 7z reader of the LZMA SDK as they come, for the archives of
# the games: built without our warnings
ARCHIVE_OBJ := \
	src/miniz/miniz.o \
	src/lzma/7zAlloc.o src/lzma/7zArcIn.o src/lzma/7zBuf.o src/lzma/7zBuf2.o src/lzma/7zCrc.o \
	src/lzma/7zCrcOpt.o src/lzma/7zDec.o src/lzma/7zFile.o src/lzma/7zStream.o src/lzma/Bcj2.o \
	src/lzma/Bra.o src/lzma/Bra86.o src/lzma/BraIA64.o src/lzma/CpuArch.o src/lzma/Delta.o \
	src/lzma/Lzma2Dec.o src/lzma/LzmaDec.o src/lzma/Ppmd7.o src/lzma/Ppmd7Dec.o

ARCHIVE_FLAGS := $(OPT_FLAGS) $(FP_FLAGS) $(PGO_FLAGS) $(DEP_FLAGS)

all: bin/odin spirv

bin/odin: $(OBJ) $(IMGUI_OBJ) $(ARCHIVE_OBJ)
	$(LD) $+ -o $@ $(CXXFLAGS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

src/imgui/%.o: src/imgui/%.cpp
	$(CXX) -c $< -o $@ $(IMGUI_FLAGS)

src/miniz/%.o: src/miniz/%.c
	$(CC) -c $< -o $@ $(ARCHIVE_FLAGS)

src/lzma/%.o: src/lzma/%.c
	$(CC) -c $< -o $@ $(ARCHIVE_FLAGS)

src/odin/rom_files.o: CXXFLAGS += -isystem src/miniz -isystem src/lzma
src/odin/host_graphics_vk.o: CXXFLAGS += $(VULKAN_FLAGS)

# The pictures of the menu, put in the binary as their files are
src/odin/menu_pictures.o: src/odin/menu_pictures.S $(wildcard src/odin/pictures/*.png)
	$(CXX) -c $< -o $@ -Wa,-I,src/odin

# The font of the menu, the same way
src/odin/menu_fonts.o: src/odin/menu_fonts.S src/odin/fonts/cozette.ttf
	$(CXX) -c $< -o $@ -Wa,-I,src/odin

# The texts of the shaders, the same way
src/odin/glsl_texts.o: src/odin/glsl_texts.S $(wildcard src/odin/glsl/*)
	$(CXX) -c $< -o $@ -Wa,-I,src/odin

# The shaders of Vulkan: compiled to SPIR-V from the same files (glslang
# defines VULKAN), then put in the binary the same way
GLSLANG := glslangValidator
SPIRV := $(patsubst %,src/odin/glsl/%.spv,whole_picture.vert blend.frag compose.vert compose.frag present.frag \
	mesh.vert mesh.geom mesh.frag mesh_collect.frag sort.frag image.vert image.frag whole_window.vert blur.frag show.frag \
	average.frag sides.frag)
src/odin/glsl/%.spv: src/odin/glsl/%
	{ echo '#version 450'; cat $<; } | $(GLSLANG) -V --stdin -S $(subst .,,$(suffix $<)) -o $@
# The shaders of the translucent fragments: their lists first, in the set
# LISTS_SET (board_translucent_vk.cpp); the meshes that collect them apart
src/odin/glsl/sort.frag.spv: src/odin/glsl/sort.frag src/odin/glsl/fragment_lists.glsl
	{ echo '#version 450'; echo '#define LISTS_SET 0'; cat src/odin/glsl/fragment_lists.glsl $<; } | \
		$(GLSLANG) -V --stdin -S frag -o $@
src/odin/glsl/mesh_collect.frag.spv: src/odin/glsl/mesh.frag src/odin/glsl/fragment_lists.glsl
	{ echo '#version 450'; echo '#define COLLECT 1'; echo '#define LISTS_SET 3'; \
		cat src/odin/glsl/fragment_lists.glsl $<; } | $(GLSLANG) -V --stdin -S frag -o $@
src/odin/spirv.o: src/odin/spirv.S $(SPIRV)
	$(CXX) -c $< -o $@ -Wa,-I,src/odin
# The filters of the picture for Vulkan: each pass of a preset in SPIR-V
# beside it, its two parts (filter_spirv.awk, picture_filter_vk.cpp)
FILTER_GLSL := $(wildcard filters/*/shaders/*.glsl)
FILTER_SPIRV := $(FILTER_GLSL:=.vert.spv) $(FILTER_GLSL:=.frag.spv)
filters/%.glsl.vert.spv: filters/%.glsl $(FILTER_GLSL) src/odin/filter_spirv.awk
	awk -v part=VERTEX -f src/odin/filter_spirv.awk $(dir $<)*.glsl $< | \
		$(GLSLANG) -V -R --amb --aml --sub vert 8 --stdin -S vert -o $@
filters/%.glsl.frag.spv: filters/%.glsl $(FILTER_GLSL) src/odin/filter_spirv.awk
	awk -v part=FRAGMENT -f src/odin/filter_spirv.awk $(dir $<)*.glsl $< | \
		$(GLSLANG) -V -R --amb --aml --stdin -S frag -o $@
spirv: $(SPIRV) $(FILTER_SPIRV)

-include $(patsubst %.o,%.d,$(OBJ) $(IMGUI_OBJ) $(ARCHIVE_OBJ))

# The commit the binary is built from, for the menu; made again with each
# commit
VERSION := $(shell git rev-parse --short HEAD 2>/dev/null)
GIT_DIR := $(shell git rev-parse --git-dir 2>/dev/null)
src/odin/version.o: CXXFLAGS += -DODIN_VERSION='"$(VERSION)"'
src/odin/version.o: $(wildcard $(GIT_DIR)/HEAD $(GIT_DIR)/index)

install:
	@( mkdir -pv $(HOME)/.local/bin )
	@( cp -v bin/odin $(HOME)/.local/bin )
	@( mkdir -pv $(HOME)/.local/share/odin/shaders )
	@( cp -v odin-games.json $(HOME)/.local/share/odin )
	@( cp -v shaders/*.txt $(HOME)/.local/share/odin/shaders )
	@( cp -rv filters $(HOME)/.local/share/odin )

clean:
	find . -name '*.o' -o -name '*.d' -o -name '*.spv' | xargs rm -vf
	rm -vf bin/*
