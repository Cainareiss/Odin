/*
 * SwanStationPS5 - dlmalloc 2.8.6 (public domain) built as a single mspace for
 * ps5_heap.c: every segment it maps comes from there (direct memory, given back
 * when a segment is empty), thread-safe spin locks.
 * The configuration follows the PS5 platform layer's (ps5platform src/dlmalloc/mspace.c,
 * Copyright (C) 2026 Mihawk, GPL-3.0-or-later, github.com/mihawk-99/PS5_PayloadSDK).
 */
#if defined(__PROSPERO__)
#include <errno.h>
#include <stddef.h>

void *ps5p_heap_map(size_t bytes);
int ps5p_heap_unmap(void *address, size_t bytes);

#define ONLY_MSPACES 1
#define MSPACES 1
#define USE_LOCKS 1
#define HAVE_MORECORE 0
#define HAVE_MMAP 1
#define HAVE_MREMAP 0
#define MMAP(s) ps5p_heap_map(s)
#define DIRECT_MMAP(s) ps5p_heap_map(s)
#define MUNMAP(a, s) ps5p_heap_unmap((a), (s))
#define MMAP_CLEARS 0
#define LACKS_SYS_MMAN_H 1
#define DEFAULT_GRANULARITY ((size_t)16 << 20)
#define DEFAULT_MMAP_THRESHOLD ((size_t)32 << 20)
#define MALLOC_FAILURE_ACTION errno = ENOMEM
#define NO_MALLOC_STATS 1
#define NO_MALLINFO 1
#define malloc_getpagesize ((size_t)0x10000)
/* The PS5 compiler assumes operator new returns 32-byte aligned memory
 * (__STDCPP_DEFAULT_NEW_ALIGNMENT__) and zeroes new objects with 32-byte
 * AVX stores (vmovaps ymm): 16 crashed SPIRV-Cross. */
#define MALLOC_ALIGNMENT ((size_t)32U)
#include "../../third_party/dlmalloc/malloc.c"
#endif
