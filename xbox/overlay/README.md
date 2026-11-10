# overlay: odin_core.dll (M1)

Everything that changes Odin for the Xbox lives here. The upstream tree
(`C:\claude-work\odinuwp\odin`) is never edited.

## Layout

- `apply_patches.sh`: rebuilds `..\core\src` from `..\core\pristine\src` (git archive of
  Odin's HEAD, the git-index blobs) and the series `patches\*.patch`, with `git apply`.
- `mkpatch.sh` / `mkpatch.py`: writes the next patch from the edits made in `core\src` since
  the snapshot `core\state` (the snapshot advances per patch, so the series is the sequence of calls).
- `patches\`: the patch series (P00, P01, P02, P04, P05, P06, P07, P08, P09, P10, P14, and X1).
  X1 is not in the port plan: `GameList::load_text` (the embedded game list).
- `CMakeLists.txt`: the DLL target `odin_core` (SHARED, UWP, static libc++, AppContainer).
- `src\`: our sources: `odin_core.cpp` (the C ABI), `odin_io.cpp` (odin::io over the host's
  functions or the app partition), `odin_session.*` (the null screen, the pad panel, the loaded game),
  `odin_sdl_subset.cpp` (SDL's name tables and the Vulkan loader stubs), `odin_selftest.cpp`
  (the core's selftests), `odin_games_json.S` (`odin-games.json` embedded with `.incbin`),
  `odin_shims.c` (the `__imp_GetActiveProcessorCount` slot).
- `include\odin_c.h`: the C ABI (the only header the frontend includes).
- `selftest\`: `odin_selftest.exe`, the desktop console driver that LoadLibrary()s the DLL.
- `build_core.sh`: configure and build (`O3` for the package, `O1` for the selftest loop).

## Build order

    bash C:/claude-work/odinuwp/overlay/apply_patches.sh
    bash C:/claude-work/odinuwp/overlay/build_core.sh O3   # -> core/build-O3/odin_core.dll

The selftest driver is built with `tools\toolchain-odin-desktop.cmake` from `selftest\`.

## Rules

- A patch changes an upstream file only where the port needs it, and says why in its comments.
- No file of the game, the BIOS or the artwork is in this tree. `odin-games.json` is read from the
  upstream clone and embedded in the DLL; no CRC table ships beside the DLL.
- Patch files are LF except `lzma/7zFile.h` (CRLF in the git index); keep the CRLF there.
