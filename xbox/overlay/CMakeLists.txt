# odin_core.dll (M1): Odin's machine core, game list and file I/O, behind the C ABI of include/odin_c.h.
# Built for the Xbox with llvm-mingw UWP (tools/toolchain-odin-uwp.cmake); see build_core.sh.
#
# Sources: core/src (the patched copy of upstream odin/src, made by apply_patches.sh from core/pristine/src and
# patches/*.patch), overlay/src (ours), and the build's SPIR-V blobs (build_spirv.sh). Nothing here is a game, a
# BIOS or artwork. GPL-3.0-or-later, as Odin.
cmake_minimum_required(VERSION 3.20)
project(odin_core C CXX)

# Assembly is assembled by the C++ driver (the .S files need its preprocessor), as upstream does
set(CMAKE_ASM_COMPILER "${CMAKE_CXX_COMPILER}")
enable_language(ASM)

get_filename_component(ODIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(ODIN_SRC "${ODIN_ROOT}/core/src" CACHE PATH "patched copy of upstream odin/src (apply_patches.sh)")
set(ODIN_UPSTREAM "${ODIN_ROOT}/odin" CACHE PATH "upstream clone: odin-games.json is read from here, never written")
set(ODIN_SPIRV_DIR "${ODIN_ROOT}/build/spirv" CACHE PATH "SPIR-V blobs of build_spirv.sh (glsl/*.spv)")
set(ODIN_JANSSON_SRC "${ODIN_ROOT}/third_party/jansson" CACHE PATH "jansson 2.15.1 (Odin's pinned version)")
set(ODIN_VULKAN_HEADERS "${ODIN_ROOT}/third_party/vulkan_headers" CACHE PATH "vulkan/ and vk_video/ only (363)")
set(ODIN_SDL_HEADERS "${ODIN_ROOT}/third_party/sdl3_headers" CACHE PATH "SDL3 headers (no SDL code)")
set(ODIN_OPT "-O3" CACHE STRING "optimisation of the core (-O1 for the selftest loop)")
option(ODIN_SELFTEST "build the selftests into the DLL (odin_selftest entry)" ON)

# The shared flags of Odin's Xbox build (PORT-PLAN 2.3): exact float, Zen 2 target, no LTO, no -march=native.
set(ODIN_FLAGS ${ODIN_OPT} -fomit-frame-pointer -fno-stack-protector -ffp-contract=off -falign-functions=64
    -march=x86-64-v3 -mtune=znver2)

# jansson (the game list reader), built as Odin builds it, without the CryptGenRandom seed (advapi32 is not in the
# app partition; the seed only randomises hash order)
set(JANSSON_BUILD_SHARED_LIBS OFF)
set(JANSSON_EXAMPLES OFF)
set(JANSSON_WITHOUT_TESTS ON)
set(JANSSON_BUILD_DOCS OFF)
set(JANSSON_INSTALL OFF)
set(USE_WINDOWS_CRYPTOAPI OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(${ODIN_JANSSON_SRC} jansson EXCLUDE_FROM_ALL)
target_compile_options(jansson PRIVATE ${ODIN_FLAGS})

# Odin's machine layer: every .cpp of machine/, the arm64 backends excluded (they are #if-guarded upstream; the
# x86-64 build does not use them).
file(GLOB_RECURSE ODIN_MACHINE CONFIGURE_DEPENDS ${ODIN_SRC}/odin/machine/*.cpp)
list(FILTER ODIN_MACHINE EXCLUDE REGEX "arm64")

# The host's pieces the machine needs (M1): game list, ROM sets and archives, user files, settings, the controls'
# bindings and roles (for the describe table), the Vulkan context's common part (picture filter), CRC of zips.
set(ODIN_HOST
    odin/common/cabinet_settings.cpp
    odin/common/wav_file.cpp
    odin/host/app/games.cpp
    odin/host/app/rom_files.cpp
    odin/host/app/user_files.cpp
    odin/host/app/settings.cpp
    odin/host/app/zip_crc.cpp
    odin/host/input/control_panel.cpp
    odin/host/input/bindings.cpp
    odin/host/input/roles.cpp
    odin/host/input/keyboard.cpp
    odin/host/app/hotkeys.cpp
    odin/host/gfx/vk_common.cpp
    odin/host/video/picture_filter_vk.cpp
    odin/host/video/picture_preset.cpp)

# LZMA SDK and miniz (the archives), as Odin builds them
set(ODIN_ARCHIVE
    lzma/7zAlloc.c lzma/7zArcIn.c lzma/7zBuf.c lzma/7zBuf2.c lzma/7zCrc.c lzma/7zCrcOpt.c lzma/7zDec.c
    lzma/7zFile.c lzma/7zStream.c lzma/Bcj2.c lzma/Bra.c lzma/Bra86.c lzma/BraIA64.c lzma/CpuArch.c lzma/Delta.c
    lzma/Lzma2Dec.c lzma/LzmaDec.c lzma/Ppmd7.c lzma/Ppmd7Dec.c
    miniz/miniz.c)

list(TRANSFORM ODIN_HOST PREPEND "${ODIN_SRC}/")
list(TRANSFORM ODIN_ARCHIVE PREPEND "${ODIN_SRC}/")
file(GLOB ODIN_SPIRV_BLOBS CONFIGURE_DEPENDS ${ODIN_SPIRV_DIR}/glsl/*.spv)

# M3: the Hikaru renderer and its Vulkan drawing (no SDL: the window is the card's image, see odin_graphics_vk.cpp)
set(ODIN_RENDERER
    odin/host/ui/loading_screen.cpp
    odin/host/renderer/hikaru/renderer.cpp
    odin/host/renderer/hikaru/renderer_debug.cpp
    odin/host/renderer/hikaru/renderer_layers.cpp
    odin/host/renderer/hikaru/renderer_programs.cpp
    odin/host/renderer/hikaru/renderer_scene.cpp
    odin/host/renderer/hikaru/renderer_state.cpp
    odin/host/renderer/hikaru/renderer_textures.cpp
    odin/host/renderer/hikaru/renderer_times.cpp
    odin/host/renderer/hikaru/renderer_translucent.cpp
    odin/host/renderer/hikaru/vk/drawing_vk.cpp
    odin/host/renderer/hikaru/vk/pictures_vk.cpp
    odin/host/renderer/hikaru/vk/pmx_vk.cpp
    odin/host/renderer/hikaru/vk/scene_vk.cpp
    odin/host/renderer/hikaru/vk/translucent_vk.cpp)
list(TRANSFORM ODIN_RENDERER PREPEND "${ODIN_SRC}/")

add_library(odin_core SHARED
    ${ODIN_MACHINE} ${ODIN_HOST} ${ODIN_ARCHIVE} ${ODIN_RENDERER}
    ${ODIN_SRC}/odin/host/gfx/spirv.S
    src/odin_games_json.S
    src/odin_shims.c
    src/odin_core.cpp
    src/odin_io.cpp
    src/odin_session.cpp
    src/odin_sdl_subset.cpp
    src/odin_sockets_stub.cpp
    src/odin_selftest.cpp
    src/odin_graphics_vk.cpp
    src/odin_screen.cpp
    src/odin_picture_test.cpp)
set_target_properties(odin_core PROPERTIES
    PREFIX ""
    OUTPUT_NAME odin_core
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )

# Upstream sources keep their own spelling of includes: "odin/...", "lzma/...", "miniz/...", "imgui/..." from src/
target_include_directories(odin_core PRIVATE
    ${ODIN_SRC}
    ${ODIN_SRC}/lzma
    ${ODIN_SRC}/miniz
    ${ODIN_SDL_HEADERS}
    ${ODIN_VULKAN_HEADERS}
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_definitions(odin_core PRIVATE
    ODIN_UWP=1
    VK_ENABLE_BETA_EXTENSIONS=1
    _FILE_OFFSET_BITS=64
    _POSIX_THREAD_SAFE_FUNCTIONS=1
    NOMINMAX=1
    ODIN_SELFTEST=$<BOOL:${ODIN_SELFTEST}>)
target_compile_options(odin_core PRIVATE ${ODIN_FLAGS})
target_compile_options(odin_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-parameter>)

# miniz's CRC-32 comes from zip_crc.cpp (upstream CMakeLists.txt:311-313)
set_source_files_properties(${ODIN_SRC}/miniz/miniz.c PROPERTIES COMPILE_DEFINITIONS USE_EXTERNAL_MZCRC)
set_source_files_properties(${ODIN_SRC}/odin/host/app/zip_crc.cpp PROPERTIES
    COMPILE_OPTIONS "-isystem;${ODIN_SRC}/miniz")

# Assembly: the SPIR-V blobs and the game list are .incbin'd through -Wa,-I
set_source_files_properties(${ODIN_SRC}/odin/host/gfx/spirv.S PROPERTIES
    COMPILE_OPTIONS "-Wa,-I,${ODIN_SPIRV_DIR}"
    OBJECT_DEPENDS "${ODIN_SPIRV_BLOBS}")
set_source_files_properties(src/odin_games_json.S PROPERTIES
    COMPILE_OPTIONS "-Wa,-I,${ODIN_UPSTREAM}"
    OBJECT_DEPENDS "${ODIN_UPSTREAM}/odin-games.json")

# The machine's own version of fopen for the LZMA SDK's FILE* streams (P08) and the upstream
# ODIN_VERSION define are not needed by the core: no version.cpp
target_link_libraries(odin_core PRIVATE jansson)
target_link_options(odin_core PRIVATE -shared -fuse-ld=lld -static -Wl,--appcontainer)
target_link_libraries(odin_core PRIVATE -lwindowsapp -lpthread)
