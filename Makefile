CXX := g++
LD := g++

DEFS := -DVK_HAVE_HIKARU

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

CXXFLAGS := -std=c++20 -pthread -I src -Wall -Wextra -Wpedantic -Wconversion -Wshadow $(OPT_FLAGS) $(PGO_FLAGS) $(DEP_FLAGS)

# The files that were C until they are written again: without the warnings
# the others are held to.
OLD_CXXFLAGS := -std=c++20 -pthread $(DEFS) -I src -I /usr/include/json -Wall -Wno-strict-aliasing -Wno-format -Wno-unused-local-typedefs $(PKG_CFLAGS) $(SDL_CFLAGS) $(OPT_FLAGS) $(PGO_FLAGS) $(DEP_FLAGS)

.PHONY: all install clean

VK_OBJ := \
	src/vk/core.o \
	src/vk/vector.o \
	src/vk/state.o \
	src/vk/buffer.o \
	src/vk/mmap.o \
	src/vk/machine.o \
	src/vk/games.o \
	src/vk/input.o \
	src/vk/renderer.o

SH4_OBJ := \
	src/cpu/sh/sh4-odin.o

ODIN_OBJ := \
	src/odin/antarctic.o \
	src/odin/antarctic_ops.o \
	src/odin/antarctic_mesh.o \
	src/odin/antarctic_disasm.o \
	src/odin/antarctic_list.o \
	src/odin/antarctic_io.o \
	src/odin/eurasia.o \
	src/odin/hikaru_gpu.o \
	src/odin/renderer.o \
	src/odin/renderer_buffers.o \
	src/odin/renderer_scene.o \
	src/odin/renderer_translucent.o \
	src/odin/renderer_programs.o \
	src/odin/renderer_shaders.o \
	src/odin/renderer_textures.o \
	src/odin/renderer_layers.o \
	src/odin/renderer_state.o \
	src/odin/renderer_debug.o \
	src/odin/renderer_gl.o \
	src/odin/aica.o \
	src/odin/sound_board.o \
	src/odin/arm7.o \
	src/odin/arm7_ops.o \
	src/odin/z80.o \
	src/odin/z80_ops.o \
	src/odin/eeprom_93c46.o \
	src/odin/x76f100.o \
	src/odin/crypt_315_5881.o \
	src/odin/actel_m1.o \
	src/odin/jvs_host.o \
	src/odin/jvs_io_13551.o \
	src/odin/card_reader.o \
	src/odin/mie.o \
	src/odin/mie_bridge.o \
	src/odin/sh4.o \
	src/odin/sh4_ops.o \
	src/odin/sh4_onchip.o \
	src/odin/scheduler.o \
	src/odin/worker.o

MACH_OBJ := \
	src/mach/hikaru/hikaru.o \
	src/mach/hikaru/hikaru-mscomm.o \
	src/mach/hikaru/hikaru-mie.o \
	src/mach/hikaru/hikaru-memctl.o \
	src/mach/hikaru/hikaru-renderer.o \
	src/mach/hikaru/hikaru-gpu.o \
	src/mach/hikaru/hikaru-aica.o

OLD_OBJ := $(VK_OBJ) $(SH4_OBJ) $(MACH_OBJ) src/vk/main.o

all: bin/odin

bin/odin: $(OLD_OBJ) $(ODIN_OBJ)
	$(LD) $+ -o $@ $(CXXFLAGS) $(LDFLAGS)

$(OLD_OBJ): CXXFLAGS := $(OLD_CXXFLAGS)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

-include $(patsubst %.o,%.d,$(OLD_OBJ) $(ODIN_OBJ))

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

