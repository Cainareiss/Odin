# odin_selftest.exe: the desktop console driver of odin_core.dll (M1). It LoadLibrary()s the UWP-built DLL, so it is
# built with the desktop triple (tools/toolchain-odin-desktop.cmake). The DLL is built by ../CMakeLists.txt.
#   cmake -S overlay/selftest -B core/build-selftest -G Ninja -DCMAKE_MAKE_PROGRAM=... \
#         -DCMAKE_TOOLCHAIN_FILE=tools/toolchain-odin-desktop.cmake
cmake_minimum_required(VERSION 3.20)
project(odin_selftest C)

add_executable(odin_selftest odin_selftest.c)
target_include_directories(odin_selftest PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
target_compile_definitions(odin_selftest PRIVATE WIN32_LEAN_AND_MEAN=1 NOMINMAX=1)
target_compile_options(odin_selftest PRIVATE -O1 -Wall -Wno-unused-function)
target_link_options(odin_selftest PRIVATE -static)

# M3: the picture driver (odin_picture.exe): the ICD, the core, odin_selftest_picture
add_executable(odin_picture odin_picture.c)
target_include_directories(odin_picture PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include)
target_compile_definitions(odin_picture PRIVATE WIN32_LEAN_AND_MEAN=1 NOMINMAX=1)
target_compile_options(odin_picture PRIVATE -O1 -Wall -Wno-unused-function)
target_link_options(odin_picture PRIVATE -static)
