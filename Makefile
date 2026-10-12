CC := gcc
CXX := g++
LD := g++

DEFS := -DVK_HAVE_HIKARU

SDL_CFLAGS := `sdl2-config --cflags`
SDL_LDFLAGS := `sdl2-config --libs`

PKG_CFLAGS := `pkg-config --cflags gl glew jansson`
PKG_LDFLAGS := `pkg-config --libs gl glew jansson`

COMMON_FLAGS = $(DEFS) -I src -I /usr/include/json -Wall -Wno-strict-aliasing -Wno-format -Wno-unused-local-typedefs

CFLAGS  := $(COMMON_FLAGS) $(PKG_CFLAGS) $(SDL_CFLAGS) -O3 -fomit-frame-pointer -flto -march=native -fno-stack-protector
#CFLAGS  := $(COMMON_FLAGS) $(PKG_CFLAGS) $(SDL_CFLAGS) -O0 -g
LDFLAGS := -lm $(PKG_LDFLAGS) $(SDL_LDFLAGS)

# Profile-guided build, 4 to 15 % faster: make clean, make PGO=generate, run
# the games for a while, make clean (the profiles stay), make PGO=use.
ifeq ($(PGO),generate)
PGO_FLAGS := -fprofile-generate -fprofile-update=single
endif
ifeq ($(PGO),use)
PGO_FLAGS := -fprofile-use -fprofile-correction -Wno-missing-profile
endif
CFLAGS += $(PGO_FLAGS)

# Each object notes the headers it was built from, in a .d file next to it.
DEP_FLAGS := -MMD -MP
CFLAGS += $(DEP_FLAGS)

CXXFLAGS := -std=c++20 -I src -Wall -Wextra -Wpedantic -Wconversion -Wshadow -O3 -fomit-frame-pointer -flto -march=native -fno-stack-protector $(PGO_FLAGS) $(DEP_FLAGS)

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
	src/odin/hikaru_gpu_bridge.o \
	src/odin/aica.o \
	src/odin/aica_bridge.o \
	src/odin/arm7.o \
	src/odin/arm7_ops.o \
	src/odin/z80.o \
	src/odin/z80_ops.o \
	src/odin/eeprom_93c46.o \
	src/odin/eeprom_bridge.o \
	src/odin/x76f100.o \
	src/odin/x76f100_bridge.o \
	src/odin/crypt_315_5881.o \
	src/odin/crypt_315_5881_bridge.o \
	src/odin/actel_m1.o \
	src/odin/actel_m1_bridge.o \
	src/odin/jvs_host.o \
	src/odin/jvs_io_13551.o \
	src/odin/card_reader.o \
	src/odin/mie.o \
	src/odin/mie_bridge.o \
	src/odin/sh4.o \
	src/odin/sh4_ops.o \
	src/odin/sh4_onchip.o \
	src/odin/sh4_bridge.o \
	src/odin/scheduler.o \
	src/odin/scheduler_bridge.o

HIKARU_OBJ := \
	$(SH4_OBJ) \
	$(ODIN_OBJ) \
	src/mach/hikaru/hikaru.o \
	src/mach/hikaru/hikaru-mscomm.o \
	src/mach/hikaru/hikaru-mie.o \
	src/mach/hikaru/hikaru-memctl.o \
	src/mach/hikaru/hikaru-renderer.o \
	src/mach/hikaru/hikaru-gpu.o \
	src/mach/hikaru/hikaru-aica.o

all: bin/odin

bin/odin: $(VK_OBJ) $(HIKARU_OBJ) src/vk/main.o
	$(LD) $+ -o $@ $(CFLAGS) $(LDFLAGS) 

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

%.o: %.c %.h
	$(CC) -c $< -o $@ $(CFLAGS)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

-include $(patsubst %.o,%.d,$(VK_OBJ) $(HIKARU_OBJ) src/vk/main.o)

# A product is fused with a sum only where the code says std::fma.
src/odin/sh4_ops.o: CXXFLAGS += -ffp-contract=off

install:
	@( mkdir -pv $(HOME)/.local/bin )
	@( cp -v bin/odin $(HOME)/.local/bin )
	@( mkdir -pv $(HOME)/.local/share/odin )
	@( cp -v vk-games.json $(HOME)/.local/share/odin )

clean:
	find . -name '*.o' -o -name '*.d' | xargs rm -vf
	rm -vf bin/*

