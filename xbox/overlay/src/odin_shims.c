// Odin Probe Hello (package #0): the one import libc++ needs that the UWP API set does not have.
//
// Derived from the Savage Wind UWP host (MIT, Copyright (c) 2026 Revivedx and the Savage Wind Recomp contributors,
// runtime/platform/uwp/uwp_io.c, sw_active_processor_count): llvm-mingw's libc++ (a desktop build) imports
// GetActiveProcessorCount for std::thread::hardware_concurrency; the UWP API set (libwindowsapp.a) has no such function,
// so the app would not load on the console. Defining the import slot here lets the linker resolve libc++'s reference to
// __imp_GetActiveProcessorCount inside the exe, with a function that asks GetSystemInfo.
// SPDX-License-Identifier: MIT AND GPL-3.0-or-later
#include <windows.h>

static DWORD WINAPI hello_active_processor_count(WORD group) {
    (void)group;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors;
}

#ifndef HELLO_NO_SHIM
DWORD(WINAPI *__imp_GetActiveProcessorCount)(WORD) = hello_active_processor_count;
#endif
