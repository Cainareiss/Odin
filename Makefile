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

.PHONY: all install clean spirv check-layers

# The front end, the board, then its chips
OBJ := \
	src/odin/host/app/main.o \
	src/odin/host/app/cpu_check.o \
	src/odin/host/app/options.o \
	src/odin/host/ui/menu_session.o \
	src/odin/host/ui/menu_session_input.o \
	src/odin/host/ui/menu_session_state.o \
	src/odin/host/app/session.o \
	src/odin/host/app/library.o \
	src/odin/host/video/display.o \
	src/odin/host/video/display_bezel.o \
	src/odin/host/video/display_window.o \
	src/odin/host/gfx/host_graphics_gl.o \
	src/odin/host/gfx/host_graphics_vk.o \
	src/odin/host/gfx/vk_common.o \
	src/odin/host/renderer/hikaru/vk/drawing_vk.o \
	src/odin/host/renderer/hikaru/vk/pictures_vk.o \
	src/odin/host/renderer/hikaru/vk/scene_vk.o \
	src/odin/host/renderer/hikaru/vk/translucent_vk.o \
	src/odin/host/renderer/hikaru/gl/drawing_gl.o \
	src/odin/host/renderer/hikaru/gl/pictures_gl.o \
	src/odin/host/renderer/hikaru/gl/programs_gl.o \
	src/odin/host/renderer/hikaru/gl/scene_gl.o \
	src/odin/host/renderer/hikaru/gl/translucent_gl.o \
	src/odin/host/video/x11_window.o \
	src/odin/host/ui/menu.o \
	src/odin/host/ui/menu_input.o \
	src/odin/host/ui/menu_pages.o \
	src/odin/host/ui/menu_library.o \
	src/odin/host/ui/menu_bar.o \
	src/odin/host/ui/cabinet_view.o \
	src/odin/host/ui/menu_kit.o \
	src/odin/host/ui/menu_nav.o \
	src/odin/host/ui/menu_pictures.o \
	src/odin/host/ui/menu_fonts.o \
	src/odin/host/ui/system_fonts.o \
	src/odin/host/app/rom_files.o \
	src/odin/host/ui/folder_dialog.o \
	src/odin/host/ui/pad_view.o \
	src/odin/host/ui/pad_drawings.o \
	src/odin/host/ui/pad_drawings_al2009man.o \
	src/odin/host/input/keyboard.o \
	src/odin/host/app/log_file.o \
	src/odin/host/app/version.o \
	src/odin/host/app/hotkeys.o \
	src/odin/host/input/controls.o \
	src/odin/host/input/pointers.o \
	src/odin/host/input/wheel_force.o \
	src/odin/host/input/evdev.o \
	src/odin/host/input/rawinput.o \
	src/odin/host/input/outputs_network.o \
	src/odin/host/input/sockets.o \
	src/odin/common/cabinet_settings.o \
	src/odin/host/input/control_panel.o \
	src/odin/host/input/bindings.o \
	src/odin/host/app/settings.o \
	src/odin/host/app/games.o \
	src/odin/host/input/roles.o \
	src/odin/machine/core/state_file.o \
	src/odin/host/app/user_files.o \
	src/odin/machine/core/worker.o \
	src/odin/machine/boards/hikaru/hikaru.o \
	src/odin/machine/boards/hikaru/hikaru_netbd.o \
	src/odin/machine/boards/hikaru/hikaru_bus.o \
	src/odin/machine/boards/hikaru/hikaru_state.o \
	src/odin/machine/core/scheduler.o \
	src/odin/machine/chips/memctl.o \
	src/odin/machine/boards/hikaru/rom_board.o \
	src/odin/machine/chips/eeprom_93c46.o \
	src/odin/machine/chips/x76f100.o \
	src/odin/machine/chips/crypt_315_5881.o \
	src/odin/machine/chips/actel_m1.o \
	src/odin/machine/cpu/sh4/sh4.o \
	src/odin/machine/cpu/sh4/sh4_fsca.o \
	src/odin/machine/core/host_code.o \
	src/odin/machine/cpu/sh4/sh4_host.o \
	src/odin/machine/cpu/sh4/sh4_translate.o \
	src/odin/machine/cpu/sh4/sh4_ops.o \
	src/odin/machine/cpu/sh4/sh4_onchip.o \
	src/odin/machine/cpu/sh4/sh4_clock.o \
	src/odin/machine/boards/hikaru/mie_board.o \
	src/odin/machine/chips/mie.o \
	src/odin/machine/cpu/z80/z80.o \
	src/odin/machine/cpu/z80/z80_ops.o \
	src/odin/machine/cabinet/jvs.o \
	src/odin/machine/cabinet/io_837_13551_imitation.o \
	src/odin/machine/cabinet/io_837_13741_imitation.o \
	src/odin/machine/cabinet/io_837_13551.o \
	src/odin/machine/chips/tmp90ph44.o \
	src/odin/machine/cabinet/io_837_13741.o \
	src/odin/machine/chips/d71054.o \
	src/odin/machine/cabinet/drive_838_13366.o \
	src/odin/machine/cabinet/netbd_837_13404.o \
	src/odin/machine/cabinet/netbd_ring.o \
	src/odin/machine/cpu/m68000/m68000.o \
	src/odin/machine/cpu/m68000/m68000_ops.o \
	src/odin/machine/cpu/tlcs90/tlcs90.o \
	src/odin/machine/chips/card_reader.o \
	src/odin/machine/chips/von4_card.o \
	src/odin/machine/boards/hikaru/sound_board.o \
	src/odin/machine/chips/aica/aica.o \
	src/odin/machine/chips/aica/aica_dsp.o \
	src/odin/host/audio/audio_out.o \
	src/odin/machine/chips/aica/aica_voice.o \
	src/odin/common/wav_file.o \
	src/odin/machine/cpu/arm7/arm7.o \
	src/odin/machine/cpu/arm7/arm7_ops.o \
	src/odin/machine/cpu/arm7/arm7_host.o \
	src/odin/machine/cpu/arm7/arm7_translate.o \
	src/odin/machine/boards/hikaru/hikaru_gpu.o \
	src/odin/machine/chips/antarctic/antarctic.o \
	src/odin/machine/chips/antarctic/antarctic_ops.o \
	src/odin/machine/chips/antarctic/antarctic_mesh.o \
	src/odin/machine/chips/antarctic/antarctic_disasm.o \
	src/odin/machine/chips/antarctic/antarctic_list.o \
	src/odin/machine/chips/antarctic/antarctic_io.o \
	src/odin/machine/chips/eurasia/eurasia.o \
	src/odin/host/renderer/hikaru/renderer.o \
	src/odin/host/renderer/hikaru/renderer_scene.o \
	src/odin/host/renderer/hikaru/renderer_translucent.o \
	src/odin/host/renderer/hikaru/renderer_programs.o \
	src/odin/host/gfx/program_cache.o \
	src/odin/host/gfx/glsl_texts.o \
	src/odin/host/gfx/spirv.o \
	src/odin/host/renderer/hikaru/renderer_textures.o \
	src/odin/host/renderer/hikaru/renderer_times.o \
	src/odin/host/renderer/hikaru/renderer_layers.o \
	src/odin/host/renderer/hikaru/renderer_state.o \
	src/odin/host/renderer/hikaru/renderer_debug.o \
	src/odin/host/gfx/gl_common.o \
	src/odin/host/video/picture_filter_gl.o \
	src/odin/host/video/picture_filter_vk.o \
	src/odin/host/video/picture_preset.o \
	src/odin/host/ui/loading_screen.o

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

src/odin/host/app/rom_files.o: CXXFLAGS += -isystem src/miniz -isystem src/lzma
src/odin/host/gfx/host_graphics_vk.o: CXXFLAGS += $(VULKAN_FLAGS)

# The pictures of the menu, put in the binary as their files are
src/odin/host/ui/menu_pictures.o: src/odin/host/ui/menu_pictures.S $(wildcard src/odin/host/ui/pictures/*.png)
	$(CXX) -c $< -o $@ -Wa,-I,src/odin/host/ui

# The font of the menu, the same way
src/odin/host/ui/menu_fonts.o: src/odin/host/ui/menu_fonts.S src/odin/host/ui/fonts/cozette.ttf
	$(CXX) -c $< -o $@ -Wa,-I,src/odin/host/ui

# The texts of the shaders, the same way
src/odin/host/gfx/glsl_texts.o: src/odin/host/gfx/glsl_texts.S $(wildcard src/odin/host/gfx/glsl/*)
	$(CXX) -c $< -o $@ -Wa,-I,src/odin/host/gfx

# The shaders of Vulkan: compiled to SPIR-V from the same files (glslang
# defines VULKAN), then put in the binary the same way
GLSLANG := glslangValidator
SPIRV := $(patsubst %,src/odin/host/gfx/glsl/%.spv,whole_picture.vert blend.frag compose.vert compose.frag present.frag \
	mesh.vert mesh.geom mesh.frag mesh_collect.frag sort.frag image.vert image.frag whole_window.vert blur.frag show.frag \
	average.frag sides.frag)
src/odin/host/gfx/glsl/%.spv: src/odin/host/gfx/glsl/%
	{ echo '#version 450'; cat $<; } | $(GLSLANG) -V --stdin -S $(subst .,,$(suffix $<)) -o $@
# The shaders of the translucent fragments: their lists first, in the set
# LISTS_SET (translucent_vk.cpp); the meshes that collect them apart
src/odin/host/gfx/glsl/sort.frag.spv: src/odin/host/gfx/glsl/sort.frag src/odin/host/gfx/glsl/fragment_lists.glsl
	{ echo '#version 450'; echo '#define LISTS_SET 0'; cat src/odin/host/gfx/glsl/fragment_lists.glsl $<; } | \
		$(GLSLANG) -V --stdin -S frag -o $@
src/odin/host/gfx/glsl/mesh_collect.frag.spv: src/odin/host/gfx/glsl/mesh.frag src/odin/host/gfx/glsl/fragment_lists.glsl
	{ echo '#version 450'; echo '#define COLLECT 1'; echo '#define LISTS_SET 3'; \
		cat src/odin/host/gfx/glsl/fragment_lists.glsl $<; } | $(GLSLANG) -V --stdin -S frag -o $@
src/odin/host/gfx/spirv.o: src/odin/host/gfx/spirv.S $(SPIRV)
	$(CXX) -c $< -o $@ -Wa,-I,src/odin/host/gfx
# The filters of the picture for Vulkan: each pass of a preset in SPIR-V
# beside it, its two parts (filter_spirv.awk, picture_filter_vk.cpp)
FILTER_GLSL := $(wildcard filters/*/shaders/*.glsl)
FILTER_SPIRV := $(FILTER_GLSL:=.vert.spv) $(FILTER_GLSL:=.frag.spv)
filters/%.glsl.vert.spv: filters/%.glsl $(FILTER_GLSL) src/odin/host/video/filter_spirv.awk
	awk -v part=VERTEX -f src/odin/host/video/filter_spirv.awk $(dir $<)*.glsl $< | \
		$(GLSLANG) -V -R --amb --aml --sub vert 8 --stdin -S vert -o $@
filters/%.glsl.frag.spv: filters/%.glsl $(FILTER_GLSL) src/odin/host/video/filter_spirv.awk
	awk -v part=FRAGMENT -f src/odin/host/video/filter_spirv.awk $(dir $<)*.glsl $< | \
		$(GLSLANG) -V -R --amb --aml --stdin -S frag -o $@
spirv: $(SPIRV) $(FILTER_SPIRV)

-include $(patsubst %.o,%.d,$(OBJ) $(IMGUI_OBJ) $(ARCHIVE_OBJ))

# The commit the binary is built from, for the menu; made again with each
# commit
VERSION := $(shell git rev-parse --short HEAD 2>/dev/null)
GIT_DIR := $(shell git rev-parse --git-dir 2>/dev/null)
src/odin/host/app/version.o: CXXFLAGS += -DODIN_VERSION='"$(VERSION)"'
src/odin/host/app/version.o: $(wildcard $(GIT_DIR)/HEAD $(GIT_DIR)/index)

# The layers, one way only: common <- machine <- host (CODING.md, section
# 2). Fails on an include of the host by the machine, of the machine or the
# host by common, of a graphics or interface library or of a header of the
# system by either, but the known ones named here
LAYER_INCLUDE := ^[[:space:]]*\#[[:space:]]*include[[:space:]]*
LAYER_FILES := --include='*.cpp' --include='*.hpp' --include='*.h'
check-layers:
	@bad=$$( \
		grep -rnE $(LAYER_FILES) '$(LAYER_INCLUDE)"odin/host/' src/odin/machine | grep -vE \
			-e '^src/odin/machine/boards/hikaru/hikaru\.hpp:[0-9]+:#include "odin/host/app/games\.hpp"$$' \
			-e '^src/odin/machine/boards/hikaru/hikaru\.hpp:[0-9]+:#include "odin/host/app/user_files\.hpp"$$' \
			-e '^src/odin/machine/chips/card_reader\.cpp:[0-9]+:#include "odin/host/app/user_files\.hpp"$$' \
			-e '^src/odin/machine/cabinet/netbd_ring\.cpp:[0-9]+:#include "odin/host/input/sockets\.hpp"$$'; \
		grep -rnE $(LAYER_FILES) '$(LAYER_INCLUDE)"odin/(machine|host)/' src/odin/common; \
		grep -rnE $(LAYER_FILES) '$(LAYER_INCLUDE)[<"](SDL|GL/|vulkan|jansson|imgui)' src/odin/machine src/odin/common; \
		grep -rnE $(LAYER_FILES) '$(LAYER_INCLUDE)[<"](windows\.h|winsock|ws2tcpip|sys/|unistd|netdb|netinet|fcntl|dlfcn)' \
			src/odin/machine src/odin/common | grep -vE \
			-e '^src/odin/machine/core/host_code\.cpp:[0-9]+:#include <(windows|sys/mman)\.h>$$' \
			-e '^src/odin/machine/cabinet/netbd_ring\.cpp:[0-9]+:#include <windows\.h>$$'); \
	if [ -n "$$bad" ]; then echo "$$bad"; echo "check-layers: includes across the layers"; exit 1; fi; \
	echo "check-layers: common <- machine <- host, only the known exceptions"

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
