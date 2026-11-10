cmake_minimum_required(VERSION 3.28)
project(ODIN_UWP C CXX)

# Odin (experimental) v0.2 shell for UWP / Xbox: the universal frontend (SwanStationPS5 fork, GPL-3) with the Odin host
# (odin_host\host_odin.cpp), which loads odin_core.dll (ABI 1) from the package. The core has no picture yet (M3).
# Built without TSUGARU_UWP (the FM Towns / Tsugaru branch is off); ARES_UWP stays as the frontend's UWP switch.
# Configure (Git Bash, MSYS_NO_PATHCONV=1):
#   cmake -S C:/claude-work/odinuwp/uwp -B C:/claude-work/odinuwp/build/uwp-stub -G "Visual Studio 17 2022" -A x64
#         -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0.26100.0 -DCMAKE_SYSTEM_PROCESSOR=AMD64
# Build target odin_uwp (Release); package with tools/package_odin.ps1.

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SW ${CMAKE_CURRENT_SOURCE_DIR}/swanui)
set(OH ${CMAKE_CURRENT_SOURCE_DIR}/odin_host)
set(BR ${CMAKE_CURRENT_SOURCE_DIR}/bridge)

################################################################################
# The frontend (plain C, GPL-3.0) plus the Odin stubs
################################################################################
set(SWANUI_FILES
  src/art.c src/bezels.c src/cheats.c src/config.c src/controls.c src/covers.c src/disc.c src/i18n.c src/library.c
  src/main.c src/play.c src/profiles.c src/stats.c src/stb_impl.c src/tips.c src/util.c src/vendor.c
  src/core/bios_choices.c src/core/swanstation_options.c
  src/ui/case3d.c src/ui/cheat_search.c src/ui/coverflow.c src/ui/draw.c src/ui/guide_screen.c src/ui/icons.c
  src/ui/login_screen.c src/ui/manual_screen.c src/ui/memcards_screen.c src/ui/menu_screen.c
  src/ui/profile_screen.c src/ui/settings_screen.c src/ui/sfx.c src/ui/stats_screen.c src/ui/text.c
  src/ui/textinput_screen.c src/ui/theme.c
  src/platform/xbr.c src/platform/blit.c third_party/rcheevos/src/rhash/md5.c
)
list(TRANSFORM SWANUI_FILES PREPEND ${SW}/)
file(GLOB SWANUI_COMPAT ${SW}/compat/*.c)
add_library(swanui STATIC ${SWANUI_FILES} ${SWANUI_COMPAT} ${OH}/odin_stubs.c)
target_compile_definitions(swanui PRIVATE SWANUI_XBOX=1 ARES_UWP=1 ODIN_UWP=1 _UWP _LIB UNICODE _UNICODE
  NOMINMAX _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
target_include_directories(swanui PRIVATE
  ${SW}/compat ${SW}/src ${SW}/third_party/stb
  ${SW}/third_party/swanstation/dep/libretro-common/include ${OH}/stub_inc)
target_compile_options(swanui PRIVATE /FI${SW}/compat/swanui_compat.h /std:c17 /utf-8 /sdl-
  /wd4100 /wd4146 /wd4018 /wd4244 /wd4267 /wd4996 /wd4068)

################################################################################
# The stub host (C++20): core/host.h, swanui_host_xbox.h, swanui_sysopts.h, tsugaru_uwp_host.h
################################################################################
add_library(odin_host STATIC ${OH}/host_odin.cpp ${OH}/odin_selftest_app.cpp)
# odin_c.h: the C ABI of odin_core.dll (overlay\include, the core's own header; the DLL is packaged beside the exe)
target_include_directories(odin_host PRIVATE ${SW} ${SW}/src ${SW}/compat ${BR} ${CMAKE_CURRENT_SOURCE_DIR}/../overlay/include)
target_compile_definitions(odin_host PRIVATE SWANUI_XBOX=1 ARES_UWP=1 ODIN_UWP=1 _UWP UNICODE _UNICODE
  NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(odin_host PRIVATE /utf-8 /sdl- /EHsc /MP)

################################################################################
# The application
################################################################################
set(ODIN_APP_SOURCES
  app/uwp_app.cpp
  ${BR}/plat_d3d11.cpp
  ${BR}/net_uwp.cpp
  ${BR}/folder_uwp.cpp
  ${BR}/storage_uwp.cpp
  ${BR}/ui_uwp.cpp
  ${BR}/gamedb_uwp.cpp
  ${BR}/uwp_paths.cpp
  ${BR}/uwp_stubs.cpp
)
add_executable(odin_uwp WIN32 ${ODIN_APP_SOURCES})
target_include_directories(odin_uwp PRIVATE ${BR} ${SW} ${SW}/compat ${SW}/src)
target_compile_definitions(odin_uwp PRIVATE SWANUI_XBOX=1 ARES_UWP=1 ODIN_UWP=1 _UWP UNICODE _UNICODE NOMINMAX
  _CRT_SECURE_NO_WARNINGS)
target_compile_options(odin_uwp PRIVATE /utf-8 /sdl- /EHsc /MP)
target_link_libraries(odin_uwp PRIVATE
  swanui odin_host
  d3d11 dxgi xaudio2 xinputuap WindowsApp ws2_32 runtimeobject)
set_target_properties(odin_uwp PROPERTIES
  VS_GLOBAL_GenerateAppxPackageOnBuild false
  VS_GLOBAL_AppxPackage false
  VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION 10.0.17134.0
  CXX_STANDARD 20)
