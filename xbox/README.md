# Odin for Xbox (UWP): experimental port

An experimental Xbox (dev-mode UWP / MSIX) port of [Odin](https://github.com/Tovarichtch/Odin), the Sega Hikaru
and Gaelco ATV Track low-level emulator (GPL-3.0-or-later).

- `overlay/`: the Xbox core, kept outside the upstream sources. `odin_core.dll` is built for UWP with llvm-mingw and
  exposes a C ABI (`include/odin_c.h`). The changes are a numbered patch series in `patches/`, applied to a copy of
  the upstream sources by `apply_patches.sh`. The upstream tree is not modified.
- `uwp/`: the experimental app. It loads the core with `LoadPackagedLibrary`, implements the file layer on top of
  the frontend's brokered Xbox file functions, and draws the picture through the frontend's D3D11 presenter. The
  frontend is derived from SwanStation (GPL-3).

## Status

- The core builds for UWP, passes the import checks, and its self-tests pass on the PC (29 of 29 on the desktop
  driver).
- The picture path runs on the PC: the core creates a headless Vulkan device through Mesa Dozen (Intel HD 620),
  and the app receives the frames. The only non-black picture the PC run produced is Odin's loading screen, because
  no game was available to test with.
- The app cannot use executable memory without the `codeGeneration` capability, so it runs the interpreter. The JIT
  is faster but is not enabled in this manifest.
- **Not yet verified on an Xbox console**, and no game has been run anywhere yet: the picture with a real game,
  the Dozen driver on the console, the MSVC runtime on the console, the folder picker, USB, HDR, and controllers.

## Not included in this repository

- Sega BIOS files (Hikaru, Mie, boot ROMs), game sets (ROMs), and Sega artwork. Supply your own MAME sets.
- Built binaries: `odin_core.dll`, the app package, certificates. Build them yourself.
- Third-party components you need to bring: a Mesa Dozen build that provides `vulkan_dzn.dll`, and Microsoft's
  `dxil.dll`.
- PlayStation artwork and sound, third-party cheat and art index lists, and internal development notes.

## Licences

- Odin and the core patches: GPL-3.0-or-later (see `LICENSE` at the root).
- Frontend and app (`uwp/`): GPL-3.0, derived from SwanStation. The licence text is in `uwp/credits/` when the
  package is built, and the original notices are kept in the source files.
