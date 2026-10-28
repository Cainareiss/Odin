cmake_minimum_required(VERSION 3.20)
project(odin LANGUAGES C CXX)

# Assemble the .S files with the C++ compiler driver
if(NOT CMAKE_ASM_COMPILER)
	set(CMAKE_ASM_COMPILER "${CMAKE_CXX_COMPILER}")
endif()
enable_language(ASM)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)

option(ODIN_NATIVE "Tune for this machine's CPU (-march=native)" ON)
option(ODIN_LTO "Link-time optimisation (-flto)" ON)
option(ODIN_TESTS "Build the tests" OFF)
set(ODIN_OPT_FLAGS "-O3 -fomit-frame-pointer -fno-stack-protector" CACHE STRING
	"Optimisation flags")
set(ODIN_PGO "" CACHE STRING "Profile-guided build: generate or use")
set_property(CACHE ODIN_PGO PROPERTY STRINGS "" generate use)
set(ODIN_VERSION "" CACHE STRING "Version string (default: git commit)")
find_program(GLSLANG glslangValidator)
if(NOT GLSLANG)
	message(FATAL_ERROR "glslangValidator not found: set -DGLSLANG=<path>")
endif()
find_program(AWK NAMES awk gawk mawk REQUIRED)

separate_arguments(OPT_FLAGS UNIX_COMMAND "${ODIN_OPT_FLAGS}")
if(ODIN_LTO)
	list(APPEND OPT_FLAGS -flto)
endif()
if(ODIN_NATIVE)
	if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
		list(APPEND OPT_FLAGS -mcpu=native)
	else()
		list(APPEND OPT_FLAGS -march=native)
	endif()
endif()
# No implicit FMA: identical results on every CPU
list(APPEND OPT_FLAGS -ffp-contract=off)
if(ODIN_PGO STREQUAL "generate")
	list(APPEND OPT_FLAGS -fprofile-generate -fprofile-update=single)
elseif(ODIN_PGO STREQUAL "use")
	list(APPEND OPT_FLAGS -fprofile-use -fprofile-correction -Wno-missing-profile)
elseif(NOT ODIN_PGO STREQUAL "")
	message(FATAL_ERROR "ODIN_PGO is generate, use or empty, not '${ODIN_PGO}'")
endif()

# Raw pkg-config flags: pkg_check_modules would put -I/usr/include first
function(odin_pkg_config var)
	execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --print-errors --exists ${ARGN} RESULT_VARIABLE result)
	if(NOT result EQUAL 0)
		message(FATAL_ERROR "pkg-config: ${ARGN} not found")
	endif()
	foreach(kind cflags libs)
		execute_process(COMMAND ${PKG_CONFIG_EXECUTABLE} --${kind} ${ARGN}
			OUTPUT_VARIABLE out OUTPUT_STRIP_TRAILING_WHITESPACE)
		separate_arguments(out UNIX_COMMAND "${out}")
		string(TOUPPER ${kind} kind)
		set(${var}_${kind} ${out} PARENT_SCOPE)
	endforeach()
endfunction()
odin_pkg_config(SDL sdl3)
if(APPLE)
	odin_pkg_config(DEPS glew jansson vulkan)
	odin_pkg_config(DEPS_LINK glew jansson)
	list(APPEND DEPS_LINK_LIBS "-framework OpenGL")
	set(HOST_WARN_FLAGS -Wno-sign-conversion)
elseif(UNIX)
	odin_pkg_config(DEPS gl glew jansson x11)
	set(DEPS_LINK_LIBS ${DEPS_LIBS})
else()
	odin_pkg_config(DEPS glew jansson)
	set(DEPS_LINK_LIBS ${DEPS_LIBS})
endif()

set(ODIN_SOURCES
	host/app/main.cpp host/app/cpu_check.cpp host/app/options.cpp
	host/ui/menu_session.cpp host/ui/menu_session_input.cpp
	host/ui/menu_session_state.cpp host/app/session.cpp
	host/app/library.cpp host/video/display.cpp
	host/video/display_bezel.cpp host/video/display_window.cpp
	host/gfx/host_graphics_gl.cpp host/gfx/host_graphics_vk.cpp
	host/gfx/vk_common.cpp host/renderer/hikaru/vk/drawing_vk.cpp
	host/renderer/hikaru/vk/pictures_vk.cpp
	host/renderer/hikaru/vk/scene_vk.cpp
	host/renderer/hikaru/vk/translucent_vk.cpp
	host/renderer/hikaru/gl/drawing_gl.cpp
	host/renderer/hikaru/gl/pictures_gl.cpp
	host/renderer/hikaru/gl/programs_gl.cpp
	host/renderer/hikaru/gl/scene_gl.cpp
	host/renderer/hikaru/gl/translucent_gl.cpp
	host/video/x11_window.cpp host/ui/menu.cpp host/ui/menu_input.cpp
	host/ui/menu_pages.cpp host/ui/menu_library.cpp
	host/ui/menu_bar.cpp host/ui/cabinet_view.cpp
	host/ui/menu_kit.cpp host/ui/menu_nav.cpp
	host/ui/system_fonts.cpp host/app/rom_files.cpp
	host/ui/folder_dialog.cpp host/ui/pad_view.cpp
	host/ui/pad_drawings.cpp host/ui/pad_drawings_al2009man.cpp
	host/input/keyboard.cpp host/app/log_file.cpp
	host/app/version.cpp host/app/hotkeys.cpp host/input/controls.cpp
	host/input/pointers.cpp host/input/wheel_force.cpp
	host/input/evdev.cpp host/input/rawinput.cpp
	host/input/outputs_network.cpp host/input/sockets.cpp
	common/cabinet_settings.cpp host/input/control_panel.cpp
	host/input/bindings.cpp host/app/settings.cpp host/app/games.cpp
	host/input/roles.cpp machine/core/state_file.cpp
	host/app/user_files.cpp machine/core/worker.cpp
	machine/boards/hikaru/hikaru.cpp
	machine/boards/hikaru/hikaru_netbd.cpp
	machine/boards/hikaru/hikaru_bus.cpp
	machine/boards/hikaru/hikaru_state.cpp machine/core/scheduler.cpp
	machine/chips/memctl.cpp machine/boards/hikaru/rom_board.cpp
	machine/chips/eeprom_93c46.cpp machine/chips/x76f100.cpp
	machine/chips/crypt_315_5881.cpp machine/chips/actel_m1.cpp
	machine/cpu/sh4/sh4.cpp machine/cpu/sh4/sh4_fsca.cpp
	machine/core/host_code.cpp machine/core/host_code_arm64.cpp
	machine/cpu/sh4/sh4_host.cpp machine/cpu/sh4/sh4_host_arm64.cpp
	machine/cpu/sh4/sh4_translate.cpp machine/cpu/sh4/sh4_ops.cpp
	machine/cpu/sh4/sh4_onchip.cpp machine/cpu/sh4/sh4_clock.cpp
	machine/boards/hikaru/mie_board.cpp machine/chips/mie.cpp
	machine/cpu/z80/z80.cpp machine/cpu/z80/z80_ops.cpp
	machine/cabinet/jvs.cpp
	machine/cabinet/io_837_13551_imitation.cpp
	machine/cabinet/io_837_13741_imitation.cpp
	machine/cabinet/io_837_13551.cpp machine/chips/tmp90ph44.cpp
	machine/cabinet/io_837_13741.cpp machine/chips/d71054.cpp
	machine/cabinet/drive_838_13366.cpp
	machine/cabinet/netbd_837_13404.cpp
	machine/cabinet/netbd_ring.cpp machine/cpu/m68000/m68000.cpp
	machine/cpu/m68000/m68000_ops.cpp machine/cpu/tlcs90/tlcs90.cpp
	machine/chips/card_reader.cpp machine/chips/von4_card.cpp
	machine/boards/hikaru/sound_board.cpp machine/chips/aica/aica.cpp
	machine/chips/aica/aica_dsp.cpp host/audio/audio_out.cpp
	machine/chips/aica/aica_voice.cpp common/wav_file.cpp
	machine/cpu/arm7/arm7.cpp machine/cpu/arm7/arm7_ops.cpp
	machine/cpu/arm7/arm7_host.cpp
	machine/cpu/arm7/arm7_host_arm64.cpp
	machine/cpu/arm7/arm7_translate.cpp
	machine/boards/hikaru/hikaru_gpu.cpp
	machine/chips/antarctic/antarctic.cpp
	machine/chips/antarctic/antarctic_ops.cpp
	machine/chips/antarctic/antarctic_mesh.cpp
	machine/chips/antarctic/antarctic_disasm.cpp
	machine/chips/antarctic/antarctic_list.cpp
	machine/chips/antarctic/antarctic_io.cpp
	machine/chips/eurasia/eurasia.cpp
	host/renderer/hikaru/renderer.cpp
	host/renderer/hikaru/renderer_scene.cpp
	host/renderer/hikaru/renderer_translucent.cpp
	host/renderer/hikaru/renderer_programs.cpp
	host/gfx/program_cache.cpp
	host/renderer/hikaru/renderer_textures.cpp
	host/renderer/hikaru/renderer_times.cpp
	host/renderer/hikaru/renderer_layers.cpp
	host/renderer/hikaru/renderer_state.cpp
	host/renderer/hikaru/renderer_debug.cpp host/gfx/gl_common.cpp
	host/video/picture_filter_gl.cpp host/video/picture_filter_vk.cpp
	host/video/picture_preset.cpp host/ui/loading_screen.cpp)
list(TRANSFORM ODIN_SOURCES PREPEND src/odin/)

# Third-party code, built without Odin's warnings
set(IMGUI_SOURCES imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
	imgui_impl_sdl3.cpp imgui_impl_opengl3.cpp imgui_impl_vulkan.cpp)
list(TRANSFORM IMGUI_SOURCES PREPEND src/imgui/)

set(ARCHIVE_SOURCES 7zAlloc.c 7zArcIn.c 7zBuf.c 7zBuf2.c 7zCrc.c
	7zCrcOpt.c 7zDec.c 7zFile.c 7zStream.c Bcj2.c
	Bra.c Bra86.c BraIA64.c CpuArch.c Delta.c
	Lzma2Dec.c LzmaDec.c Ppmd7.c Ppmd7Dec.c)
list(TRANSFORM ARCHIVE_SOURCES PREPEND src/lzma/)
list(PREPEND ARCHIVE_SOURCES src/miniz/miniz.c)

# ImGui uses Odin's Vulkan function pointers
set(VULKAN_FLAGS IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

add_library(odin_imgui OBJECT ${IMGUI_SOURCES})
set_target_properties(odin_imgui PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_include_directories(odin_imgui PRIVATE src/imgui)
target_compile_definitions(odin_imgui PRIVATE ${VULKAN_FLAGS})
target_compile_options(odin_imgui PRIVATE ${SDL_CFLAGS} ${OPT_FLAGS})

add_library(odin_archive OBJECT ${ARCHIVE_SOURCES})
target_compile_options(odin_archive PRIVATE ${OPT_FLAGS})

# Vulkan shaders to SPIR-V
set(GLSL ${CMAKE_SOURCE_DIR}/src/odin/host/gfx/glsl)
set(SPIRV_DIR ${CMAKE_BINARY_DIR}/spirv)
file(MAKE_DIRECTORY ${SPIRV_DIR}/glsl)
set(SPIRV)
foreach(shader whole_picture.vert blend.frag compose.vert compose.frag present.frag
		mesh.vert mesh.geom mesh.frag sort.frag image.vert image.frag whole_window.vert
		blur.frag show.frag average.frag sides.frag)
	string(REGEX REPLACE ".*\\." "" stage ${shader})
	set(out ${SPIRV_DIR}/glsl/${shader}.spv)
	if(shader STREQUAL "sort.frag")
		set(script "{ echo '#version 450'; echo '#define LISTS_SET 0'; cat \"$2\" \"$3\"; } | \"$0\" -V --stdin -S frag -o \"$1\"")
		set(inputs ${GLSL}/fragment_lists.glsl ${GLSL}/${shader})
	else()
		set(script "{ echo '#version 450'; cat \"$2\"; } | \"$0\" -V --stdin -S ${stage} -o \"$1\"")
		set(inputs ${GLSL}/${shader})
	endif()
	add_custom_command(OUTPUT ${out}
		COMMAND sh -c "${script}" ${GLSLANG} ${out} ${inputs}
		DEPENDS ${inputs} VERBATIM)
	list(APPEND SPIRV ${out})
endforeach()
set(out ${SPIRV_DIR}/glsl/mesh_collect.frag.spv)
add_custom_command(OUTPUT ${out}
	COMMAND sh -c "{ echo '#version 450'; echo '#define COLLECT 1'; echo '#define LISTS_SET 3'; cat \"$2\" \"$3\"; } | \"$0\" -V --stdin -S frag -o \"$1\""
		${GLSLANG} ${out} ${GLSL}/fragment_lists.glsl ${GLSL}/mesh.frag
	DEPENDS ${GLSL}/fragment_lists.glsl ${GLSL}/mesh.frag VERBATIM)
list(APPEND SPIRV ${out})

# Files embedded with .incbin
set(UI ${CMAKE_SOURCE_DIR}/src/odin/host/ui)
set(GFX ${CMAKE_SOURCE_DIR}/src/odin/host/gfx)
file(GLOB PICTURES CONFIGURE_DEPENDS ${UI}/pictures/*.png)
file(GLOB GLSL_FILES CONFIGURE_DEPENDS ${GLSL}/*)
list(FILTER GLSL_FILES EXCLUDE REGEX "\\.spv$")
add_library(odin_embedded OBJECT
	${UI}/menu_pictures.S ${UI}/menu_fonts.S ${GFX}/glsl_texts.S ${GFX}/spirv.S ${SPIRV})
set_source_files_properties(${UI}/menu_pictures.S PROPERTIES
	COMPILE_OPTIONS "-Wa,-I,${UI}" OBJECT_DEPENDS "${PICTURES}")
set_source_files_properties(${UI}/menu_fonts.S PROPERTIES
	COMPILE_OPTIONS "-Wa,-I,${UI}" OBJECT_DEPENDS "${UI}/fonts/cozette.ttf")
set_source_files_properties(${GFX}/glsl_texts.S PROPERTIES
	COMPILE_OPTIONS "-Wa,-I,${GFX}" OBJECT_DEPENDS "${GLSL_FILES}")
set_source_files_properties(${GFX}/spirv.S PROPERTIES
	COMPILE_OPTIONS "-Wa,-I,${SPIRV_DIR}" OBJECT_DEPENDS "${SPIRV}")

# Version from git, only when the source folder is the repository top;
# reconfigures when HEAD or the index changes
set(version "${ODIN_VERSION}")
if(version STREQUAL "")
	find_package(Git QUIET)
	if(GIT_FOUND)
		execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --show-toplevel --short HEAD
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			OUTPUT_VARIABLE git_out RESULT_VARIABLE git_result ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
		if(git_result EQUAL 0)
			string(REPLACE "\n" ";" git_out "${git_out}")
			list(GET git_out 0 git_top)
			list(GET git_out 1 git_head)
			file(REAL_PATH "${git_top}" git_top)
			file(REAL_PATH "${CMAKE_SOURCE_DIR}" source_top)
			if(git_top STREQUAL source_top)
				set(version "${git_head}")
				execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --path-format=absolute
						--git-path HEAD --git-path index --symbolic-full-name HEAD
					WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
					OUTPUT_VARIABLE git_paths ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
				string(REPLACE "\n" ";" git_paths "${git_paths}")
				foreach(path ${git_paths})
					if(path MATCHES "^refs/")
						execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --path-format=absolute --git-path ${path}
							WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
							OUTPUT_VARIABLE path ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
					endif()
					if(EXISTS "${path}")
						set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
					endif()
				endforeach()
			endif()
		endif()
	endif()
endif()
message(STATUS "Odin version: '${version}'")
set_source_files_properties(src/odin/host/app/version.cpp PROPERTIES
	COMPILE_DEFINITIONS "ODIN_VERSION=\"${version}\"")

set_source_files_properties(src/odin/host/app/rom_files.cpp PROPERTIES
	COMPILE_OPTIONS "-isystem;${CMAKE_SOURCE_DIR}/src/miniz;-isystem;${CMAKE_SOURCE_DIR}/src/lzma")
set_source_files_properties(src/odin/host/gfx/host_graphics_vk.cpp PROPERTIES
	COMPILE_DEFINITIONS "${VULKAN_FLAGS}")

add_executable(odin ${ODIN_SOURCES}
	$<TARGET_OBJECTS:odin_imgui> $<TARGET_OBJECTS:odin_archive> $<TARGET_OBJECTS:odin_embedded>)
set_target_properties(odin PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_include_directories(odin PRIVATE src)
target_compile_options(odin PRIVATE -pthread -Wall -Wextra -Wpedantic -Wconversion -Wshadow
	${HOST_WARN_FLAGS} ${DEPS_CFLAGS} ${SDL_CFLAGS} ${OPT_FLAGS})
target_link_options(odin PRIVATE -pthread ${OPT_FLAGS})
if(UNIX)
	target_link_libraries(odin PRIVATE m)
endif()
target_link_libraries(odin PRIVATE ${DEPS_LINK_LIBS} ${SDL_LIBS})

# Data in build/share/odin, laid out as installed
set(DATA ${CMAKE_BINARY_DIR}/share/odin)
file(GLOB DATA_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/shaders/*.txt)
file(GLOB_RECURSE FILTER_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/filters/*)
list(APPEND DATA_FILES odin-games.json ${FILTER_FILES})
list(FILTER DATA_FILES EXCLUDE REGEX "\\.spv$")
foreach(file ${DATA_FILES})
	configure_file(${file} ${DATA}/${file} COPYONLY)
endforeach()

# Picture filters to SPIR-V
set(FILTER_AWK ${CMAKE_SOURCE_DIR}/src/odin/host/video/filter_spirv.awk)
file(GLOB FILTER_GLSL CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/filters/*/shaders/*.glsl)
set(FILTER_SPIRV)
foreach(glsl ${FILTER_GLSL})
	file(RELATIVE_PATH rel ${CMAKE_SOURCE_DIR} ${glsl})
	get_filename_component(folder ${glsl} DIRECTORY)
	add_custom_command(OUTPUT ${DATA}/${rel}.vert.spv
		COMMAND sh -c "\"$0\" -v part=VERTEX -f \"$1\" \"$2\"/*.glsl \"$3\" | \"$4\" -V -R --amb --aml --sub vert 8 --stdin -S vert -o \"$5\""
			${AWK} ${FILTER_AWK} ${folder} ${glsl} ${GLSLANG} ${DATA}/${rel}.vert.spv
		DEPENDS ${FILTER_GLSL} ${FILTER_AWK} VERBATIM)
	add_custom_command(OUTPUT ${DATA}/${rel}.frag.spv
		COMMAND sh -c "\"$0\" -v part=FRAGMENT -f \"$1\" \"$2\"/*.glsl \"$3\" | \"$4\" -V -R --amb --aml --stdin -S frag -o \"$5\""
			${AWK} ${FILTER_AWK} ${folder} ${glsl} ${GLSLANG} ${DATA}/${rel}.frag.spv
		DEPENDS ${FILTER_GLSL} ${FILTER_AWK} VERBATIM)
	list(APPEND FILTER_SPIRV ${DATA}/${rel}.vert.spv ${DATA}/${rel}.frag.spv)
endforeach()
add_custom_target(spirv ALL DEPENDS ${FILTER_SPIRV})

install(TARGETS odin RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${DATA} DESTINATION ${CMAKE_INSTALL_DATADIR})

add_custom_target(check-layers
	COMMAND sh ${CMAKE_SOURCE_DIR}/tools/check-layers.sh
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)

if(ODIN_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
