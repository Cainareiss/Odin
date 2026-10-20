CXX := g++
LD := g++

SDL_CFLAGS := `pkg-config --cflags sdl3`
SDL_LDFLAGS := `pkg-config --libs sdl3`

PKG_CFLAGS := `pkg-config --cflags gl glew jansson`
PKG_LDFLAGS := `pkg-config --libs gl glew jansson`

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

CXXFLAGS := -std=c++20 -pthread -I src -Wall -Wextra -Wpedantic -Wconversion -Wshadow $(PKG_CFLAGS) $(SDL_CFLAGS) $(OPT_FLAGS) $(PGO_FLAGS) $(DEP_FLAGS)

.PHONY: all install clean

# The front end, the board, then its chips
OBJ := \
	src/odin/main.o \
	src/odin/display.o \
	src/odin/keyboard.o \
	src/odin/controls.o \
	src/odin/outputs_network.o \
	src/odin/control_panel.o \
	src/odin/bindings.o \
	src/odin/settings.o \
	src/odin/games.o \
	src/odin/roles.o \
	src/odin/state_file.o \
	src/odin/save_folder.o \
	src/odin/worker.o \
	src/odin/hikaru.o \
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
	src/odin/mie_board.o \
	src/odin/mie.o \
	src/odin/z80.o \
	src/odin/z80_ops.o \
	src/odin/jvs.o \
	src/odin/jvs_837_13551.o \
	src/odin/jvs_837_13741.o \
	src/odin/io_837_13551.o \
	src/odin/tmp90ph44.o \
	src/odin/io_837_13741.o \
	src/odin/d71054.o \
	src/odin/tlcs90.o \
	src/odin/card_reader.o \
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
	src/odin/renderer_buffers.o \
	src/odin/renderer_scene.o \
	src/odin/renderer_translucent.o \
	src/odin/renderer_programs.o \
	src/odin/renderer_shaders.o \
	src/odin/renderer_textures.o \
	src/odin/renderer_times.o \
	src/odin/renderer_layers.o \
	src/odin/renderer_state.o \
	src/odin/renderer_debug.o \
	src/odin/renderer_gl.o \
	src/odin/loading_screen.o

# Dear ImGui as it comes, for the menu: built without our warnings
IMGUI_OBJ := \
	src/imgui/imgui.o \
	src/imgui/imgui_draw.o \
	src/imgui/imgui_tables.o \
	src/imgui/imgui_widgets.o \
	src/imgui/imgui_impl_sdl3.o \
	src/imgui/imgui_impl_opengl3.o

IMGUI_FLAGS := -std=c++20 -I src/imgui $(SDL_CFLAGS) $(OPT_FLAGS) $(PGO_FLAGS) $(DEP_FLAGS)

all: bin/odin

bin/odin: $(OBJ) $(IMGUI_OBJ)
	$(LD) $+ -o $@ $(CXXFLAGS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

src/imgui/%.o: src/imgui/%.cpp
	$(CXX) -c $< -o $@ $(IMGUI_FLAGS)

-include $(patsubst %.o,%.d,$(OBJ) $(IMGUI_OBJ))

# A product is fused with a sum only where the code says std::fma.
src/odin/sh4_ops.o: CXXFLAGS += -ffp-contract=off

install:
	@( mkdir -pv $(HOME)/.local/bin )
	@( cp -v bin/odin $(HOME)/.local/bin )
	@( mkdir -pv $(HOME)/.local/share/odin/shaders )
	@( cp -v odin-games.json $(HOME)/.local/share/odin )
	@( cp -v shaders/*.txt $(HOME)/.local/share/odin/shaders )

clean:
	find . -name '*.o' -o -name '*.d' | xargs rm -vf
	rm -vf bin/*
