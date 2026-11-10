/*
 * SwanStation Xbox (UWP) port - <SDL2/SDL.h> subset: the threads, atomics, mutexes, condition
 * variables, semaphores and ticks the frontend uses, on Win32 primitives of the UWP API partition
 * (swanui_compat.c). No SDL video, audio or input: the Xbox platform layer does those itself.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The semantics follow SDL 2.30: return values, NULL handling, recursive mutexes, SDL_TryLockMutex
 * giving SDL_MUTEX_TIMEDOUT when the mutex is busy, and atomics that are full memory barriers.
 * SDL_CreateThread is a plain three-argument function (SDL's own Windows header turns it into a
 * macro that passes _beginthreadex). Windows.h is not included here: frontend names such as IDLE or
 * BUSY would collide with its macros.
 */
#ifndef SWANUI_SDL_H
#define SWANUI_SDL_H

/* The standard headers SDL_stdinc.h includes: the frontend relies on them (main.c calls isalnum
 * without including <ctype.h>, for example). */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include <inttypes.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>
#include <float.h>
#include <intrin.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SDL_FALSE = 0,
    SDL_TRUE = 1
} SDL_bool;

typedef uint32_t Uint32;
typedef uint64_t Uint64;

/* ---- Atomics ---------------------------------------------------------------------------------- */

typedef struct
{
    int value;
} SDL_atomic_t;

/* Sets the value, returns the previous one */
static __inline int SDL_AtomicSet(SDL_atomic_t *a, int v)
{
    return (int)_InterlockedExchange((volatile long *)&a->value, (long)v);
}

static __inline int SDL_AtomicGet(SDL_atomic_t *a)
{
    return (int)_InterlockedOr((volatile long *)&a->value, 0);
}

/* Sets newval only if the value is oldval; SDL_TRUE when it did */
static __inline SDL_bool SDL_AtomicCAS(SDL_atomic_t *a, int oldval, int newval)
{
    return _InterlockedCompareExchange((volatile long *)&a->value, (long)newval, (long)oldval) == (long)oldval
               ? SDL_TRUE
               : SDL_FALSE;
}

/* ---- Threads ---------------------------------------------------------------------------------- */

typedef struct SDL_Thread SDL_Thread;
typedef int (*SDL_ThreadFunction)(void *data);

/* NULL when the thread can't be created */
SDL_Thread *SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data);
/* Waits for the thread, stores its return value in *status (when status is not NULL), frees it */
void SDL_WaitThread(SDL_Thread *thread, int *status);
/* Lets the thread run on and free itself when it ends */
void SDL_DetachThread(SDL_Thread *thread);

/* ---- Mutexes (recursive) ---------------------------------------------------------------------- */

#define SDL_MUTEX_TIMEDOUT 1

typedef struct SDL_mutex SDL_mutex;

SDL_mutex *SDL_CreateMutex(void);
int SDL_LockMutex(SDL_mutex *mutex);    /* 0 */
int SDL_TryLockMutex(SDL_mutex *mutex); /* 0, or SDL_MUTEX_TIMEDOUT when another thread holds it */
int SDL_UnlockMutex(SDL_mutex *mutex);  /* 0 */

/* ---- Condition variables ---------------------------------------------------------------------- */

typedef struct SDL_cond SDL_cond;

SDL_cond *SDL_CreateCond(void);
int SDL_CondWait(SDL_cond *cond, SDL_mutex *mutex); /* 0, or -1 on error */
int SDL_CondSignal(SDL_cond *cond);                 /* 0, or -1 on error */

/* ---- Semaphores ------------------------------------------------------------------------------- */

typedef struct SDL_semaphore SDL_sem;

SDL_sem *SDL_CreateSemaphore(Uint32 initial_value);
int SDL_SemWait(SDL_sem *sem); /* 0, or -1 on error */
int SDL_SemPost(SDL_sem *sem); /* 0, or -1 on error */

/* ---- Time ------------------------------------------------------------------------------------- */

/* Milliseconds since the first call of either function */
Uint32 SDL_GetTicks(void);
Uint64 SDL_GetTicks64(void);

#ifdef __cplusplus
}
#endif

#endif
