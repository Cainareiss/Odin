/*
 * SwanStationPS5 - a real heap for the PS5 build.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * On hardware, ordinary malloc() of a few MB failed ("OOM for vout_buf",
 * SDL textures): the C heap a native title gets is small. SwanStationPS5 therefore
 * runs dlmalloc (public domain) on direct memory. The linker's --wrap redirects
 * every malloc/free/... in SwanStationPS5, the core and SDL here (APP_WRAP_SYMBOLS).
 * Memory the system libraries allocated themselves (strdup, fopen buffers)
 * lies outside the heap's range and goes back to the real free/realloc.
 *
 * The heap does not hold direct memory it does not use: an address range is
 * reserved at the first allocation, and dlmalloc maps a segment of direct
 * memory into it when it needs one and gives it back when the segment is
 * empty. The segment tracking and the placement outside the Vulkan driver's
 * GPU window follow the title heap of the PS5 platform layer (ps5platform
 * src/heap.c and src/placement.c, Copyright (C) 2026 Mihawk,
 * GPL-3.0-or-later, github.com/mihawk-99/PS5_PayloadSDK), with a single
 * dlmalloc mspace in place of its one for each thread.
 */
#if defined(__PROSPERO__)
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef void *mspace;
mspace create_mspace(size_t capacity, int locked);
void *mspace_malloc(mspace msp, size_t bytes);
void mspace_free(mspace msp, void *mem);
void *mspace_calloc(mspace msp, size_t n_elements, size_t elem_size);
void *mspace_realloc(mspace msp, void *mem, size_t newsize);
void *mspace_memalign(mspace msp, size_t alignment, size_t bytes);

int64_t sceKernelGetDirectMemorySize(void);
int sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end, size_t length,
                                  size_t alignment, int memory_type, int64_t *physical_address);
int sceKernelMapDirectMemory(void **address, size_t length, int protection, int flags,
                             int64_t physical_address, size_t alignment);
int sceKernelReleaseDirectMemory(int64_t start, size_t length);
int sceKernelReserveVirtualRange(void **address, size_t length, int flags, size_t alignment);
int sceKernelMunmap(void *address, size_t length);

void *__real_malloc(size_t size);
void __real_free(void *ptr);
void *__real_calloc(size_t n, size_t size);
void *__real_realloc(void *ptr, size_t size);
void *__real_memalign(size_t alignment, size_t size);
int __real_posix_memalign(void **out, size_t alignment, size_t size);

#define MEMORY_TYPE_CPU 12
#define PROT_CPU_RW 0x3
#define MAP_FIXED_FLAG 0x10
#define UNIT ((size_t)0x10000)
#define PAGE ((size_t)0x4000)
#define HEAP_HINT ((uintptr_t)0x2000000000ull)
#define RANGE_MAX ((size_t)16 << 30)
#define RANGE_MIN ((size_t)1 << 30)
#define GPU_WINDOW_LOW ((uintptr_t)0x200000000ull)
#define GPU_WINDOW_HIGH ((uintptr_t)0x300000000ull)
#define VIEW_AREA ((uintptr_t)0x400000000ull)
#define AREA_STEP ((uintptr_t)0x100000000ull)
#define AREA_END ((uintptr_t)0x10000000000ull)
#define AREA_CANDIDATES ((unsigned)((AREA_END - VIEW_AREA) / AREA_STEP))
#define MAX_SEGMENTS 1024

static mspace heap;
static uint8_t *arena_lo;
static size_t range_bytes;
static int init_state; /* 0 not tried, 1 ready, -1 failed: use the system heap */

typedef struct
{
    size_t offset;
    size_t bytes;
    int64_t direct_start;
} Segment;

static Segment segments[MAX_SEGMENTS];
static unsigned segment_count;
static size_t mapped_bytes;
static pthread_mutex_t segment_lock = PTHREAD_MUTEX_INITIALIZER;

static int outside_window(uintptr_t base, size_t bytes)
{
    return base >= GPU_WINDOW_HIGH || base + bytes <= GPU_WINDOW_LOW;
}

static void *reserve_placed(size_t bytes)
{
    for (unsigned attempt = 0; attempt <= AREA_CANDIDATES; ++attempt)
    {
        uintptr_t at = attempt == 0 ? HEAP_HINT : VIEW_AREA + (uintptr_t)(attempt - 1) * AREA_STEP;
        if (!outside_window(at, bytes))
            continue;
        void *placed = (void *)at;
        if (sceKernelReserveVirtualRange(&placed, bytes, 0, UNIT) != 0)
            continue;
        if (outside_window((uintptr_t)placed, bytes))
            return placed;
        sceKernelMunmap(placed, bytes);
    }
    return NULL;
}

void *ps5p_heap_map(size_t bytes)
{
    pthread_mutex_lock(&segment_lock);
    void *result = (void *)-1;
    bytes = (bytes + UNIT - 1) / UNIT * UNIT;
    unsigned slot = 0;
    size_t offset = 0;
    if (bytes && segment_count < MAX_SEGMENTS)
    {
        for (; slot < segment_count; ++slot)
        {
            if (segments[slot].offset - offset >= bytes)
                break;
            offset = segments[slot].offset + segments[slot].bytes;
        }
        int64_t start = -1;
        if (offset <= range_bytes && range_bytes - offset >= bytes &&
            sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), bytes, UNIT, MEMORY_TYPE_CPU, &start) == 0)
        {
            void *at = arena_lo + offset;
            void *mapped = at;
            if (sceKernelMapDirectMemory(&mapped, bytes, PROT_CPU_RW, MAP_FIXED_FLAG, start, UNIT) != 0 || mapped != at)
                sceKernelReleaseDirectMemory(start, bytes);
            else
            {
                memmove(&segments[slot + 1], &segments[slot], (segment_count - slot) * sizeof(segments[0]));
                segments[slot] = (Segment){offset, bytes, start};
                ++segment_count;
                mapped_bytes += bytes;
                result = at;
            }
        }
    }
    pthread_mutex_unlock(&segment_lock);
    return result;
}

int ps5p_heap_unmap(void *address, size_t bytes)
{
    int result = -1;
    pthread_mutex_lock(&segment_lock);
    if ((uint8_t *)address >= arena_lo)
    {
        size_t offset = (size_t)((uint8_t *)address - arena_lo);
        unsigned first = 0;
        while (first < segment_count && segments[first].offset < offset)
            ++first;
        if (first < segment_count && segments[first].offset == offset)
        {
            unsigned last = first;
            size_t covered = 0;
            while (last < segment_count && covered < bytes && segments[last].offset == offset + covered)
            {
                covered += segments[last].bytes;
                ++last;
            }
            if (covered == bytes)
            {
                for (unsigned i = first; i < last; ++i)
                {
                    void *at = arena_lo + segments[i].offset;
                    sceKernelMunmap(at, segments[i].bytes);
                    sceKernelReserveVirtualRange(&at, segments[i].bytes, MAP_FIXED_FLAG, PAGE);
                    sceKernelReleaseDirectMemory(segments[i].direct_start, segments[i].bytes);
                    mapped_bytes -= segments[i].bytes;
                }
                memmove(&segments[first], &segments[last], (segment_count - last) * sizeof(segments[0]));
                segment_count -= last - first;
                result = 0;
            }
        }
    }
    pthread_mutex_unlock(&segment_lock);
    return result;
}

static void heap_init(void)
{
    if (init_state)
        return;
    init_state = -1;
    for (size_t bytes = RANGE_MAX; bytes >= RANGE_MIN; bytes /= 2)
    {
        void *base = reserve_placed(bytes);
        if (!base)
            continue;
        arena_lo = base;
        range_bytes = bytes;
        heap = create_mspace(0, 1);
        if (heap)
        {
            init_state = 1;
            return;
        }
        sceKernelMunmap(base, bytes);
        return;
    }
}

static int ours(const void *p)
{
    return (uintptr_t)p - (uintptr_t)arena_lo < range_bytes;
}

void *__wrap_malloc(size_t size)
{
    heap_init();
    void *p = init_state > 0 ? mspace_malloc(heap, size) : NULL;
    return p ? p : __real_malloc(size);
}

void __wrap_free(void *ptr)
{
    if (!ptr)
        return;
    if (ours(ptr))
        mspace_free(heap, ptr);
    else
        __real_free(ptr);
}

void *__wrap_calloc(size_t n, size_t size)
{
    heap_init();
    void *p = init_state > 0 ? mspace_calloc(heap, n, size) : NULL;
    return p ? p : __real_calloc(n, size);
}

void *__wrap_realloc(void *ptr, size_t size)
{
    if (!ptr)
        return __wrap_malloc(size);
    if (ours(ptr))
        return mspace_realloc(heap, ptr, size);
    return __real_realloc(ptr, size); /* allocated by a system library */
}

void *__wrap_memalign(size_t alignment, size_t size)
{
    heap_init();
    void *p = init_state > 0 ? mspace_memalign(heap, alignment, size) : NULL;
    return p ? p : __real_memalign(alignment, size);
}

int __wrap_posix_memalign(void **out, size_t alignment, size_t size)
{
    heap_init();
    void *p = init_state > 0 ? mspace_memalign(heap, alignment, size) : NULL;
    if (p)
    {
        *out = p;
        return 0;
    }
    return __real_posix_memalign(out, alignment, size);
}

void *__wrap_aligned_alloc(size_t alignment, size_t size)
{
    return __wrap_memalign(alignment, size);
}

void *__real_reallocf(void *ptr, size_t size);
size_t __real_malloc_usable_size(const void *ptr);
size_t mspace_usable_size(const void *mem);

void *__wrap_reallocf(void *ptr, size_t size)
{
    if (!ptr || ours(ptr))
    {
        void *p = __wrap_realloc(ptr, size);
        if (!p && ptr && size)
            __wrap_free(ptr);
        return p;
    }
    return __real_reallocf(ptr, size);
}

size_t __wrap_malloc_usable_size(const void *ptr)
{
    if (ptr && ours(ptr))
        return mspace_usable_size(ptr);
    return __real_malloc_usable_size(ptr);
}

/* For the log: the size of the range the heap lives in ("0" when it fell back to the system heap). */
size_t ps5_heap_size_mb(void)
{
    heap_init();
    return init_state > 0 ? range_bytes >> 20 : 0;
}
#endif
