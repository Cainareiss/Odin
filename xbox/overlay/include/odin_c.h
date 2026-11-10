/*
 * odin_c.h: the C ABI of odin_core.dll (the Odin emulation core for the Xbox
 * frontend, M1). GPL-3.0-or-later, as Odin.
 *
 * Rules of the ABI:
 *  - C only: no C++ type crosses the boundary; cdecl; the DLL is built with
 *    llvm-mingw (x86_64-w64-mingw32uwp) and loaded with LoadPackagedLibrary
 *    (or LoadLibrary by the selftest driver).
 *  - Strings are UTF-8, NUL-terminated. Text the core reports goes into a
 *    buffer the caller gives (err/cap, report/cap): nothing is allocated by one
 *    side for the other, and every function returns a status (ODIN_*).
 *  - Every export catches the C++ exceptions of the core: an exception comes
 *    back as ODIN_E_INTERNAL with the text in err.
 *  - Callbacks run on the thread that called the function that makes them
 *    (odin_frame calls audio_push, odin_load calls progress). The core makes
 *    no thread the caller does not know of except the one of the board's
 *    drawing worker (M3).
 *  - The DLL keeps no ROM, BIOS or artwork. The game list (names, sizes, CRCs,
 *    cabinets, protection keys, default EEPROM images: odin-games.json) is
 *    embedded in the DLL; no CRC table is shipped beside it.
 */

#ifndef ODIN_C_H
#define ODIN_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ODIN_CORE_BUILD
#define ODIN_API __declspec(dllexport)
#else
#define ODIN_API __declspec(dllimport)
#endif

/* The version of this header and of the functions below. odin_abi() must
 * return it before anything else is called. */
#define ODIN_ABI 2
#define ODIN_IO_ABI 1

/* Status of every function that returns int32_t. Negative is a failure. */
enum OdinStatus {
	ODIN_OK = 0,
	ODIN_E_ABI = -1,        /* the ABI of the DLL is not the one of the header */
	ODIN_E_ARG = -2,        /* a required pointer or name is null or empty */
	ODIN_E_NOT_INIT = -3,   /* odin_init was not called, or failed */
	ODIN_E_UNKNOWN_GAME = -4, /* not in the game list (or not a playable game) */
	ODIN_E_MISSING = -5,    /* the game's set is incomplete: err has the files */
	ODIN_E_BIOS_MISSING = -6, /* the shared sets (hikaru, mie) are not there */
	ODIN_E_BAD_SET = -7,    /* a set is there but cannot be read */
	ODIN_E_BOARD = -8,      /* the board refused the game (its sections) */
	ODIN_E_LOADED = -9,     /* a game is already loaded: odin_unload first */
	ODIN_E_NOT_LOADED = -10, /* no game is loaded */
	ODIN_E_IO = -11,        /* a file could not be read or written */
	ODIN_E_STOPPED = -12,   /* the emulated machine stopped; stopped() said why */
	ODIN_E_FAILED = -13,    /* a selftest failed (report has the details) */
	ODIN_E_INTERNAL = -14,  /* the core caught an exception */
	ODIN_E_OPTION = -15,    /* an unknown option, or a value out of its range */
	ODIN_E_UNSUPPORTED = -16 /* not in this build (the graphics layer is M3) */
};

/* odin_frame: the frame ran and there is no picture yet (no card, or none
 * drawn yet). */
#define ODIN_FRAME_NO_PICTURE 1
/* odin_frame: the frame ran and a picture is published (ABI 2: the latest
 * drawn one; fresh is 1 once for each picture that is new since the last call). */
#define ODIN_FRAME_PICTURE 0

/* The host's file functions. Paths are UTF-8. Each returns 0 on success. The
 * core calls them through odin::io (io.hpp): every file the core reads, lists,
 * writes, renames or removes goes through here when the host gives them;
 * without them the core uses the Win32 functions of the app partition.
 *  open:   access is ODIN_IO_READ, ODIN_IO_WRITE (create or truncate),
 *          ODIN_IO_APPEND (create or append), with ODIN_IO_TEXT for the text
 *          modes. On success *handle is a Win32 HANDLE; the core owns it from
 *          then on and closes it (through the C library).
 *  stat:   *flags = ODIN_IO_EXISTS | ODIN_IO_DIR as applicable (0: absent); *size
 *          the size of a file (0 for a folder). Absent is success with 0.
 *  list:   one call of entry() per file and folder of the folder, without . and
 *          ..; name is the entry name (not a path), is_dir 1 for a folder.
 *  mkdir:  one folder (the core makes the missing ones above it).
 *  rename_replace: replaces an existing "to".
 *  remove: a file, or an empty folder. */
#define ODIN_IO_READ 1u
#define ODIN_IO_WRITE 2u
#define ODIN_IO_APPEND 4u
#define ODIN_IO_TEXT 8u
#define ODIN_IO_EXISTS 1u
#define ODIN_IO_DIR 2u

typedef struct OdinIo {
	uint32_t abi;           /* ODIN_IO_ABI */
	void *ctx;
	int (*open)(void *ctx, const char *utf8_path, uint32_t access, void **handle);
	int (*stat)(void *ctx, const char *utf8_path, uint32_t *flags, uint64_t *size);
	int (*list)(void *ctx, const char *utf8_dir, void *sink,
	            void (*entry)(void *sink, const char *name, uint32_t is_dir, uint64_t size));
	int (*mkdir)(void *ctx, const char *utf8_path);
	int (*rename_replace)(void *ctx, const char *utf8_from, const char *utf8_to);
	int (*remove)(void *ctx, const char *utf8_path);
} OdinIo;

/* Levels of log(). */
#define ODIN_LOG_INFO 0
#define ODIN_LOG_WARN 1
#define ODIN_LOG_ERROR 2

typedef struct OdinCallbacks {
	void *ctx;
	/* a line of the log (stdout/stderr of the core go here too); may be NULL */
	void (*log)(void *ctx, int32_t level, const char *utf8_text);
	/* the emulated machine stopped for `utf8_text`: the core does not go on
	 * (its thread waits); the next odin_* call returns ODIN_E_STOPPED. May be
	 * NULL. */
	void (*stopped)(void *ctx, const char *utf8_text);
	/* loading: done of total, what is being loaded; may be NULL. ABI 2: the
	 * programs are made ready on the drawing thread while odin_load waits, so
	 * this is called from that thread (not the caller's) */
	void (*progress)(void *ctx, uint32_t done, uint32_t total, const char *utf8_what);
	/* interleaved stereo int16 samples of one frame (frames pairs, `rate`
	 * pairs per second); may be NULL */
	void (*audio_push)(void *ctx, const int16_t *stereo, uint32_t frames, uint32_t rate);
	/* NULL: the core uses the Win32 functions of the app partition */
	const OdinIo *io;
} OdinCallbacks;

typedef struct OdinInit {
	uint32_t abi;               /* ODIN_ABI */
	const char *install_dir;    /* the package folder (may be "") */
	const char *data_root;      /* writable: logs, cache, saves, states */
	OdinCallbacks callbacks;
} OdinInit;

/* Pads. The stick and trigger values are the XInput ones: stick -32768..32767
 * (+Y is down, as SDL; the frontend negates up), triggers 0..255. buttons is a
 * mask of ODIN_PAD_* (the Xbox controller). connected 0: the others are
 * ignored. */
#define ODIN_PAD_A 0x0001u
#define ODIN_PAD_B 0x0002u
#define ODIN_PAD_X 0x0004u
#define ODIN_PAD_Y 0x0008u
#define ODIN_PAD_LB 0x0010u
#define ODIN_PAD_RB 0x0020u
#define ODIN_PAD_L3 0x0040u
#define ODIN_PAD_R3 0x0080u
#define ODIN_PAD_UP 0x0100u
#define ODIN_PAD_DOWN 0x0200u
#define ODIN_PAD_LEFT 0x0400u
#define ODIN_PAD_RIGHT 0x0800u
#define ODIN_PAD_BACK 0x1000u   /* View */
#define ODIN_PAD_START 0x2000u  /* Menu */
#define ODIN_PAD_GUIDE 0x4000u

typedef struct OdinPad {
	uint32_t connected;
	uint32_t buttons;
	int16_t lx, ly, rx, ry;
	uint8_t lt, rt;
} OdinPad;

/* One catalogue entry. Strings are NUL-terminated and may be empty. The list
 * holds every game of the list and the five shared sets (is_support 1: never
 * a shelf entry). */
typedef struct OdinCatalogEntry {
	char name[32];          /* MAME's set name */
	char title[128];
	char machine[16];       /* "hikaru" or "gaelco" */
	char parent[32];        /* the set it is a version of, or "" */
	int32_t is_support;     /* 1 for hikaru, mie, jvs13551, 18wheelr, srally2 */
} OdinCatalogEntry;

/* The state of a set, as odin_check_set finds it. */
typedef struct OdinSetStatus {
	int32_t state;          /* 0 complete, 1 incomplete, -1 unreadable */
	uint32_t missing;       /* how many files of the set are missing or wrong */
} OdinSetStatus;

/* Calls, in the order a frontend makes them:
 *   odin_abi, odin_init, (odin_catalog_*, odin_check_set)*,
 *   odin_load, (odin_set_pads, odin_frame, odin_flush_saves)*, odin_unload,
 *   odin_shutdown. */

ODIN_API int32_t odin_abi(void);
ODIN_API int32_t odin_init(const OdinInit *init, char *err, size_t cap);
ODIN_API void odin_shutdown(void);

ODIN_API int32_t odin_catalog_count(void);
ODIN_API int32_t odin_catalog_get(int32_t index, OdinCatalogEntry *out);
ODIN_API int32_t odin_catalog_find(const char *name);

/* roms_dir and bios_dir: the folders the sets are looked for in (either may
 * be ""). report: one line per missing file (name, size, CRC, the set to
 * look in), at most cap bytes. quick: no content reads, only listings. */
ODIN_API int32_t odin_check_set(const char *roms_dir, const char *bios_dir, const char *name,
                                OdinSetStatus *status, char *report, size_t cap);

/* Loads a game: the sets are read (through the host's io), the board is made,
 * the power-on reset is done. saves_dir: the folder of the game's kept
 * memories (NVRAM, EEPROMs, clock); "" keeps nothing. err gets the reason:
 * the missing files, "BIOS missing: ..." when the shared sets are not there. */
ODIN_API int32_t odin_load(const char *game, const char *roms_dir, const char *bios_dir, const char *saves_dir,
                           char *err, size_t cap);
ODIN_API int32_t odin_unload(char *err, size_t cap);
ODIN_API int32_t odin_reset(char *err, size_t cap);

ODIN_API void odin_set_pads(const OdinPad *pads, uint32_t count);

/* Runs one frame (the audio goes through callbacks.audio_push). ABI 2: the
 * board runs the frame, the frame is drawn in the background, and the call
 * returns the LATEST PICTURE DRAWN (the one before this frame). w, h and
 * pitch: its size and row pitch in bytes; fresh: 1 once for each picture that
 * is new since the last call, else 0. Returns ODIN_FRAME_PICTURE (0) when a
 * picture exists, ODIN_FRAME_NO_PICTURE (1) when none is drawn yet (or there
 * is no graphics card), a negative status on failure. The pixels are read with
 * odin_picture_pixels, and are valid until the next odin_frame call. */
ODIN_API int32_t odin_frame(uint32_t *w, uint32_t *h, uint32_t *pitch, uint32_t *fresh);

/* ABI 2. The pixels of the picture odin_frame last returned: B8G8R8A8 (the
 * frontend's pixel format 1), the top line first, pitch bytes a line. NULL
 * when there is none. Valid until the next odin_frame (or odin_unload). */
ODIN_API const uint8_t *odin_picture_pixels(void);

/* ABI 2. The Vulkan ICD for the graphics card: the frontend loads the ICD
 * (LoadPackagedLibrary("vulkan_dzn.dll"), negotiates its interface version)
 * and passes its vk_icdGetInstanceProcAddr here, a function of the form
 * void *(void *instance, const char *name). The card is made at the first
 * odin_load after this call, once. Not called: no card, no picture (the board
 * still runs, as in ABI 1). */
ODIN_API void odin_set_vulkan_icd(void *get_instance_proc_addr);

/* ABI 2, a selftest of the picture path (no game): a synthetic board (its own
 * SH-4 stub, no BIOS), `frames` frames drawn and read back, then a loading
 * frame; the program lists (a path to a shaders\*.txt list, or "" for none)
 * are made ready and counted. The report says the device, the frame size, a
 * checksum of the picture, the count of non-zero pixels, the time per frame and
 * the programs. Needs odin_set_vulkan_icd and odin_init first. ODIN_OK when the
 * frames were drawn, ODIN_E_FAILED otherwise (the report says why). */
ODIN_API int32_t odin_selftest_picture(const char *programs_list, uint32_t frames, char *report, size_t cap);

/* The kept memories (NVRAM, EEPROMs, clock) to the saves folder. Called on
 * suspend, from the in-game menu and on a timer. */
ODIN_API int32_t odin_flush_saves(char *err, size_t cap);

/* One state file of the loaded game; the file is the only copy. */
ODIN_API int32_t odin_save_state(const char *path, char *err, size_t cap);
ODIN_API int32_t odin_load_state(const char *path, char *err, size_t cap);

/* Options by key: scale (1..3), anisotropy (1,2,4,8,16), volume (25..100),
 * dead_zone (0..50 percent), trigger_threshold (0..100 percent). get writes
 * the value (NUL-terminated) and returns its length, or a negative status. */
ODIN_API int32_t odin_option_get(const char *key, char *value, size_t cap);
ODIN_API int32_t odin_option_set(const char *key, const char *value);

/* Selftests (no game needed): "jit", "board", "describe", "bios". The report
 * says each check; ODIN_OK when all passed, ODIN_E_FAILED otherwise. */
ODIN_API int32_t odin_selftest(const char *name, char *report, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* ODIN_C_H */
