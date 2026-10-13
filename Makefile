CXX := g++
LD := g++

SDL_CFLAGS := `sdl2-config --cflags`
SDL_LDFLAGS := `sdl2-config --libs`

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
	src/odin/games.o \
	src/odin/state_file.o \
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
	src/odin/sh4_host.o \
	src/odin/sh4_ops.o \
	src/odin/sh4_onchip.o \
	src/odin/mie_board.o \
	src/odin/mie.o \
	src/odin/mie_stand_in.o \
	src/odin/z80.o \
	src/odin/z80_ops.o \
	src/odin/jvs_host.o \
	src/odin/jvs_io_13551.o \
	src/odin/card_reader.o \
	src/odin/sound_board.o \
	src/odin/aica.o \
	src/odin/arm7.o \
	src/odin/arm7_ops.o \
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
	src/odin/renderer_gl.o

all: bin/odin

bin/odin: $(OBJ)
	$(LD) $+ -o $@ $(CXXFLAGS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

-include $(patsubst %.o,%.d,$(OBJ))

# A product is fused with a sum only where the code says std::fma.
src/odin/sh4_ops.o: CXXFLAGS += -ffp-contract=off

install:
	@( mkdir -pv $(HOME)/.local/bin )
	@( cp -v bin/odin $(HOME)/.local/bin )
	@( mkdir -pv $(HOME)/.local/share/odin/shaders )
	@( cp -v vk-games.json $(HOME)/.local/share/odin )
	@( cp -v shaders/*.txt $(HOME)/.local/share/odin/shaders )

clean:
	find . -name '*.o' -o -name '*.d' | xargs rm -vf
	rm -vf bin/*
