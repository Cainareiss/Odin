# Odin coding charter

Odin is a hard fork of Valkyrie (Stefano Teso, GPL-3.0). Its goal is a
low-level emulation (LLE) of the Sega Hikaru arcade system. Fidelity comes
first, speed second, convenience last. This file says how new code is
written so that everyone knows what to do.

## 1. Language

- All the code is C++20, in the sober subset described in section 3.
- Build flags: `-std=c++20 -Wall -Wextra -Wpedantic -Wconversion
  -Wshadow`. Every file compiles warning-free. No compiler extensions.
  Dear ImGui (`src/imgui`), miniz (`src/miniz`) and the LZMA SDK
  (`src/lzma`) are not ours: they are kept as they come and built with
  their own flags.

## 2. One class per chip, in three layers

- One source pair per real chip or board, in the folder of its kind:
  `src/odin/machine/chips/<chip>.hpp` and `.cpp` for a chip, in a folder
  of its own when it takes several files (`machine/chips/aica/`);
  `machine/cpu/<cpu>/` for a processor; `machine/boards/<board>/` for a
  board; `machine/cabinet/` for the boards and cables of the cabinet
  around it; `machine/core/` for what runs them all (the scheduler, the
  save states). Examples: `machine/chips/mie.cpp` (315-6146),
  `machine/cpu/z80/z80.cpp`, `machine/chips/memctl.cpp` (315-6154),
  `machine/cabinet/io_837_13551_imitation.cpp`,
  `machine/chips/aica/aica.cpp`. The JVS cable and protocol shared by the
  I/O boards: `machine/cabinet/jvs.cpp`.
- Every device implements the same small interface: `reset()`,
  `read(addr, size)`, `write(addr, size, value)`, `run(cycles)` or
  `tick()`, interrupt lines in and out, `save(state&)` / `load(state&)`,
  and a trace hook (section 6).
- Inheritance is one level deep at most: an interface and its
  implementations. No deeper hierarchies, no mix-ins.
- A device knows its own silicon only. It never knows which game is
  running.
- The sources are in three layers, and an include goes one way only:
  `common` ← `machine` ← `host`.
  - `src/odin/common/`: small helpers with no API of the host, used on
    both sides.
  - `src/odin/machine/`: the emulated hardware. It includes `common/` and
    `machine/` only: no SDL, OpenGL, Vulkan, jansson or ImGui, and no
    header of the system.
  - `src/odin/host/`: the PC (the window, the renderer, the inputs, the
    sound, the menus). It may include everything.
- `machine/boards/hikaru/hikaru_draw.hpp` is the contract between the board
  and the renderer, with no graphics API: the board makes it, the renderer
  (`host/renderer/hikaru/`) reads it.
- `cmake --build build --target check-layers` fails on an include that
  crosses the layers, but these known ones. Each is to be removed later,
  in a commit of its own:

  | Machine file | Includes | Why | Later |
  |---|---|---|---|
  | `machine/boards/hikaru/hikaru.hpp` | `host/app/games.hpp` | the board gets the whole `Game` | give it only the configuration of the board it needs |
  | `machine/boards/hikaru/hikaru.hpp` | `host/app/user_files.hpp` | the paths of the saves | pass the paths in, or a small storage interface |
  | `machine/chips/card_reader.cpp` | `host/app/user_files.hpp` | the safe writing of the cards | the same as `hikaru.hpp` |
  | `machine/cabinet/netbd_ring.cpp` | `host/input/sockets.hpp`, `<windows.h>` | the TCP link between cabinets | a `Link` interface, implemented in `host/` |
  | `machine/core/host_code.cpp` | `<windows.h>`, `<sys/mman.h>` | executable memory for the translated SH-4 and ARM7 code | it stays |

## 3. The C++20 subset

Allowed and encouraged:

- `enum class`, `constexpr`, `static_assert`, `std::array`, `std::span`,
  `std::byte`, `std::bit_cast`, `std::optional`, `std::string_view`,
  `std::format` for logs, structured bindings, `if constexpr`.
- Fixed-width integers (`uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`,
  `int32_t`). Never a bare `int` for anything that models hardware.
- Simple templates for sized bus access (`read<uint16_t>(addr)`), nothing
  more clever than that.
- Ranges only where a loop would be less readable.

Not allowed:

- Exceptions, RTTI (`dynamic_cast`, `typeid`), coroutines, modules.
- Template metaprogramming, SFINAE tricks, CRTP, variadic templates
  outside of a logging helper.
- Raw `new` / `delete`. Use values, `std::unique_ptr`, or containers.
- Global mutable state. Devices own their state; the machine owns the
  devices.
- Type punning through pointer casts. Use `std::bit_cast` or `memcpy`.
- Boost or any other third-party library. Dependencies stay SDL3,
  OpenGL 4.3 core with GLEW (or OpenGL ES 3.1+ with ODIN_GLES),
  Vulkan (its headers only: the functions are loaded through SDL), and
  jansson; glslang at build time only, for the
  shaders of Vulkan. The exceptions are copied from a release, unchanged:
  Dear ImGui, which draws the menu (its core and its SDL3, OpenGL 3 and
  Vulkan backends, `src/imgui`); miniz and the 7z reader of the LZMA SDK,
  which read the archives of the games (`src/miniz`, `src/lzma`).

## 4. Hardware rules (what LLE means here)

- Emulate what the silicon does, as documented. Every non-obvious value
  (a clock, a delay, a status bit, a register default) carries a short
  comment with its source: a datasheet page, a MAME file and line, a
  measurement on the real board, or a disassembly address.
- No game-specific behaviour in device code. If a game needs a workaround,
  the bug is in the emulation, and the workaround is temporary.
- A workaround that must exist is marked `// HACK:` with one line saying
  why, and it is listed in TODO. A `HACK` without a reason is a bug.
- High-level emulation (HLE) of a component is allowed only as a
  documented, switchable stand-in while the LLE path is being built. The
  comment names the LLE path that replaces it.
- Unknown register accesses fail loud: log once with address, size, value
  and the CPU program counter. Never return a silent zero.
- Timing is driven by cycles, never by host time. A device must behave the
  same whether the host is fast or slow.

## 5. Memory and buses

- Bus accesses go through named functions with explicit size. No pointer
  arithmetic into memory buffers from device code.
- Endianness is explicit: helpers such as `le16()`, `le32()`, `be32()`.
  No assumptions about the host.
- Memory regions are `std::span<std::byte>` or `std::span<uint8_t>` with
  the region size carried along. Out-of-range is a logged error, not
  undefined behaviour.

## 6. Traces and probes

- Every device can emit a trace line in one shared format:
  `cpu pc rw addr size value`. The format matches the register logs we
  take from MAME so that the two can be compared line by line.
- Tracing costs nothing when it is off: one branch on a boolean, no clock
  reads, no string formatting before the check.
- A probe added for an investigation is removed when the investigation
  ends, or promoted to a permanent trace point with a comment.

## 7. Errors and invariants

- No exceptions. Functions that can fail return a value that says so
  (`bool`, `std::optional`, an enum).
- `assert()` guards invariants that the code itself controls. It never
  guards data that comes from a ROM or a game.
- A failed invariant stops the emulator with a message that names the
  device, the register and the value.

## 8. Style

- Tabs for indentation, as in Valkyrie and MAME. Braces on the same line
  for control flow, on their own line for functions.
- `snake_case` for functions, variables and files. `PascalCase` for types.
  `UPPER_CASE` for constants and register names that mirror the hardware
  documentation.
- Comments in plain English, short, and only where the code does not
  speak for itself. They say why, not what. No author names, no dates, no
  tool names in comments.
- A file is one chip or one subsystem. A function fits on one screen.

## 9. Commits

- One change per commit. Subject line in the imperative, plain English,
  at most 60 characters, prefixed by the area: `mie: run the Z80 at
  16 MHz`, `gpu-cp: decode opcode 0x0D1 mode 3`. The area can follow the
  folders: `sh4:`, `aica:`, `antarctic:`, `renderer:`, `menu:`. The
  prefixes already in the history (`gpu-cp:`, `mie:`) stay as they are;
  the history is never rewritten.
- The body, when needed, says what was measured and where the facts come
  from. No attribution trailers.
- Only code and project files go in the repository: sources, headers,
  build files, this charter, README, TODO. Study notes, dumps, captures,
  logs and private tools live outside.

## 10. Tests

- A device gets a test as soon as it has a reference: a golden trace from
  MAME, a datasheet sequence, or a measurement. The test replays the input
  and compares the trace.
- Runs are deterministic. The same ROM, the same inputs and the same frame
  count give the same trace, every time.
- A change that alters a golden trace explains why in the commit.

## 11. Licensing

- Odin is GPL-3.0. Code taken from MAME keeps its BSD-3-Clause header and
  its copyright holders. Code taken from Valkyrie keeps Stefano Teso's
  copyright. Credit stays where it belongs.
- Dear ImGui keeps its MIT licence and its copyright holders
  (`src/imgui/LICENSE.txt`).
- miniz keeps its MIT licence and its copyright holders
  (`src/miniz/LICENSE`); the LZMA SDK is in the public domain
  (`src/lzma/lzma-sdk.txt`).
- The gamepad drawings made from AL2009man's Gamepad-Asset-Pack keep its
  MIT licence, written at the top of `src/odin/host/ui/pad_drawings_al2009man.cpp`.
- The font of the menu, Cozette (a part of it, `src/odin/host/ui/fonts/cozette.ttf`),
  keeps its MIT licence (`src/odin/host/ui/fonts/LICENSE-cozette`).
- The CRT filters of Lola Retro (`filters/lola-retro`, its port for 86Box
  as it comes) keep their MIT licence (`filters/lola-retro/LICENSE`).
