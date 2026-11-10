/*
 * odin_selftest.exe: the test driver of odin_core.dll (M1). A desktop console
 * program (llvm-mingw x86_64-w64-mingw32, toolchain-odin-desktop.cmake): it
 * LoadLibrary()s the UWP-built odin_core.dll and runs
 *   1. the ABI smoke test through odin_c.h (init, catalogue, checks, load errors,
 *      unload, flush, options, pads, frame without a game);
 *   2. the core's own selftests (odin_selftest: jit, board, describe, bios).
 * It prints PASS or FAIL per test and exits with the number of failures.
 * No ROM, BIOS or game file is needed or looked for. GPL-3.0-or-later.
 *
 *   odin_selftest.exe [odin_core.dll] [data folder] [fixtures folder]
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "odin_c.h"

typedef int32_t (*fn_abi)(void);
typedef int32_t (*fn_init)(const OdinInit *, char *, size_t);
typedef void (*fn_shutdown)(void);
typedef int32_t (*fn_count)(void);
typedef int32_t (*fn_get)(int32_t, OdinCatalogEntry *);
typedef int32_t (*fn_find)(const char *);
typedef int32_t (*fn_check)(const char *, const char *, const char *, OdinSetStatus *, char *, size_t);
typedef int32_t (*fn_load)(const char *, const char *, const char *, const char *, char *, size_t);
typedef int32_t (*fn_err)(char *, size_t);
typedef void (*fn_pads)(const OdinPad *, uint32_t);
typedef int32_t (*fn_frame)(uint32_t *, uint32_t *, uint32_t *, uint32_t *);
typedef int32_t (*fn_text)(const char *, char *, size_t);
typedef int32_t (*fn_text2)(const char *, const char *);
typedef int32_t (*fn_selftest)(const char *, char *, size_t);
typedef int32_t (*fn_state)(const char *, char *, size_t);

static struct {
	HMODULE module;
	fn_abi abi;
	fn_init init;
	fn_shutdown shutdown;
	fn_count count;
	fn_get get;
	fn_find find;
	fn_check check;
	fn_load load;
	fn_err unload, reset, flush;
	fn_pads set_pads;
	fn_frame frame;
	fn_text option_get_;
	fn_text2 option_set_;
	fn_selftest selftest;
	fn_state save_state, load_state;
} api;

static int passed = 0, failed = 0;

static void
log_line(void *ctx, int32_t level, const char *text)
{
	(void)ctx;
	printf("  [core %s] %s\n", level == ODIN_LOG_ERROR ? "error" : level == ODIN_LOG_WARN ? "warn" : "info", text);
}

static void
verdict(const char *name, int ok, const char *detail)
{
	if (ok) {
		passed++;
		printf("PASS %s%s%s\n", name, detail && *detail ? ": " : "", detail ? detail : "");
	} else {
		failed++;
		printf("FAIL %s%s%s\n", name, detail && *detail ? ": " : "", detail ? detail : "");
	}
}

static int
resolve(const char *name, void **slot)
{
	*slot = (void *)GetProcAddress(api.module, name);
	if (!*slot) {
		printf("FAIL resolve %s: not exported (GetLastError %lu)\n", name, GetLastError());
		failed++;
		return 0;
	}
	return 1;
}

static int
load_core(const char *path)
{
	api.module = LoadLibraryA(path);
	if (!api.module) {
		printf("FAIL load %s: LoadLibrary error %lu\n", path, GetLastError());
		failed++;
		return 0;
	}
	int ok = 1;
	ok &= resolve("odin_abi", (void **)&api.abi);
	ok &= resolve("odin_init", (void **)&api.init);
	ok &= resolve("odin_shutdown", (void **)&api.shutdown);
	ok &= resolve("odin_catalog_count", (void **)&api.count);
	ok &= resolve("odin_catalog_get", (void **)&api.get);
	ok &= resolve("odin_catalog_find", (void **)&api.find);
	ok &= resolve("odin_check_set", (void **)&api.check);
	ok &= resolve("odin_load", (void **)&api.load);
	ok &= resolve("odin_unload", (void **)&api.unload);
	ok &= resolve("odin_reset", (void **)&api.reset);
	ok &= resolve("odin_flush_saves", (void **)&api.flush);
	ok &= resolve("odin_set_pads", (void **)&api.set_pads);
	ok &= resolve("odin_frame", (void **)&api.frame);
	ok &= resolve("odin_option_get", (void **)&api.option_get_);
	ok &= resolve("odin_option_set", (void **)&api.option_set_);
	ok &= resolve("odin_selftest", (void **)&api.selftest);
	ok &= resolve("odin_save_state", (void **)&api.save_state);
	ok &= resolve("odin_load_state", (void **)&api.load_state);
	return ok;
}

static void
abi_smoke(const char *data, const char *fixtures)
{
	char err[512] = "", report[1024] = "", text[64] = "";
	char empty[MAX_PATH];
	OdinInit init;
	OdinCatalogEntry entry;
	OdinSetStatus status;
	OdinPad pads[2];
	uint32_t w = 9, h = 9, pitch = 9, fresh = 9;
	int32_t n;

	snprintf(empty, sizeof(empty), "%s\\empty", fixtures);
	CreateDirectoryA(fixtures, NULL);
	CreateDirectoryA(empty, NULL);

	/* ABI version first */
	verdict("abi version", api.abi() == ODIN_ABI, "");

	/* init with the DLL's own file functions and a data folder */
	memset(&init, 0, sizeof(init));
	init.abi = ODIN_ABI;
	init.install_dir = "";
	init.data_root = data;
	init.callbacks.ctx = NULL;
	init.callbacks.log = log_line;
	n = api.init(&init, err, sizeof(err));
	verdict("odin_init", n == ODIN_OK, n == ODIN_OK ? "" : err);
	if (n != ODIN_OK)
		return;
	/* a second init is a no-op */
	verdict("odin_init twice", api.init(&init, err, sizeof(err)) == ODIN_OK, "");

	/* the catalogue: 14 games (8 Hikaru, 6 Gaelco) and the five shared sets */
	{
		int32_t count = api.count(), games = 0, hikaru = 0, gaelco = 0, support = 0, i;
		int ok_fields = 1;

		for (i = 0; i < count; i++) {
			if (api.get(i, &entry) != ODIN_OK) {
				ok_fields = 0;
				continue;
			}
			if (entry.is_support) {
				support++;
				continue;
			}
			games++;
			if (strcmp(entry.machine, "hikaru") == 0)
				hikaru++;
			else if (strcmp(entry.machine, "gaelco") == 0)
				gaelco++;
		}
		snprintf(text, sizeof(text), "%d entries: %d games (%d hikaru, %d gaelco), %d support",
		         (int)count, (int)games, (int)hikaru, (int)gaelco, (int)support);
		verdict("catalogue counts", ok_fields && games == 14 && hikaru == 8 && gaelco == 6 && support == 5, text);
	}
	{
		int32_t airtrix = api.find("airtrix"), airtrixo = api.find("airtrixo");
		int ok = airtrix >= 0 && airtrixo >= 0 && api.get(airtrixo, &entry) == ODIN_OK &&
		         strcmp(entry.parent, "airtrix") == 0 && api.find("nosuchgame") < 0;

		verdict("catalogue find and parent", ok, "airtrixo is a version of airtrix");
	}

	/* a set that is not there: complete = no, the missing files are listed */
	n = api.check(empty, empty, "airtrix", &status, report, sizeof(report));
	snprintf(text, sizeof(text), "state %d, %u missing", (int)status.state, (unsigned)status.missing);
	verdict("check set on empty folders", n == ODIN_OK && status.state == 1 && status.missing > 0, text);
	verdict("check set lists the files", n == ODIN_OK && strstr(report, "in hikaru.zip") != NULL, "");
	n = api.check(empty, empty, "nosuchgame", &status, report, sizeof(report));
	verdict("check unknown game", n == ODIN_E_UNKNOWN_GAME, "");

	/* loading a missing set: the shared BIOS is named, nothing is loaded */
	n = api.load("airtrix", empty, empty, "", err, sizeof(err));
	verdict("load with no BIOS and no set", n == ODIN_E_BIOS_MISSING && strncmp(err, "BIOS missing", 12) == 0,
	        err);
	n = api.load("nosuchgame", empty, empty, "", err, sizeof(err));
	verdict("load unknown game", n == ODIN_E_UNKNOWN_GAME, "");
	n = api.load("hikaru", empty, empty, "", err, sizeof(err));
	verdict("load a shared set is not a game", n == ODIN_E_UNKNOWN_GAME, "");
	n = api.load("", empty, empty, "", err, sizeof(err));
	verdict("load with no name", n == ODIN_E_ARG, "");

	/* with nothing loaded: unload, flush, reset, frame and states say so */
	verdict("unload with nothing loaded", api.unload(err, sizeof(err)) == ODIN_E_NOT_LOADED, "");
	verdict("flush with nothing loaded", api.flush(err, sizeof(err)) == ODIN_E_NOT_LOADED, "");
	verdict("reset with nothing loaded", api.reset(err, sizeof(err)) == ODIN_E_NOT_LOADED, "");
	n = api.frame(&w, &h, &pitch, &fresh);
	verdict("frame with nothing loaded", n == ODIN_E_NOT_LOADED && w == 0 && h == 0 && fresh == 0, "");

	/* the pads take a snapshot at any time */
	memset(pads, 0, sizeof(pads));
	pads[0].connected = 1;
	pads[0].buttons = ODIN_PAD_A | ODIN_PAD_START;
	api.set_pads(pads, 2);
	verdict("set pads", 1, "");

	/* options: range and key checks */
	verdict("option scale 2", api.option_set_("scale", "2") == ODIN_OK, "");
	n = api.option_get_("scale", text, sizeof(text));
	verdict("option scale reads back", n > 0 && strcmp(text, "2") == 0, text);
	verdict("option scale 7 refused", api.option_set_("scale", "7") == ODIN_E_OPTION, "");
	verdict("option anisotropy 3 refused", api.option_set_("anisotropy", "3") == ODIN_E_OPTION, "");
	verdict("option unknown key refused", api.option_set_("nosuchkey", "1") == ODIN_E_OPTION, "");
	verdict("option default", api.option_get_("volume", text, sizeof(text)) > 0 && strcmp(text, "100") == 0, text);

	/* the states without a game */
	verdict("save state with nothing loaded", api.save_state("nothing.odinstate", err, sizeof(err)) == ODIN_E_NOT_LOADED, "");
	verdict("load state with nothing loaded", api.load_state("nothing.odinstate", err, sizeof(err)) == ODIN_E_NOT_LOADED, "");

	api.shutdown();
}

static void
core_selftests(void)
{
	static const char *const names[] = { "jit", "board", "describe", "bios" };
	size_t i;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		char report[8192] = "";
		int32_t n = api.selftest(names[i], report, sizeof(report));
		char name[64];

		snprintf(name, sizeof(name), "selftest %s", names[i]);
		/* the report of each test is its lines, as the core wrote them */
		printf("---- %s\n%s", name, report);
		verdict(name, n == ODIN_OK, "");
	}
}

int
main(int argc, char **argv)
{
	const char *dll = argc > 1 ? argv[1] : "odin_core.dll";
	const char *data = argc > 2 ? argv[2] : "odin-selftest-data";
	const char *fixtures = argc > 3 ? argv[3] : "fixtures";

	printf("odin_selftest: %s\n", dll);
	if (!load_core(dll)) {
		printf("RESULT 0 passed, %d failed (the DLL did not load)\n", failed);
		return 1;
	}
	abi_smoke(data, fixtures);
	/* the core's selftests run after the smoke test has shut the core down:
	 * they init it themselves */
	{
		char err[512] = "";
		OdinInit init;

		memset(&init, 0, sizeof(init));
		init.abi = ODIN_ABI;
		init.data_root = data;
		init.callbacks.log = log_line;
		if (api.init(&init, err, sizeof(err)) == ODIN_OK) {
			core_selftests();
			api.shutdown();
		} else {
			verdict("odin_init for the selftests", 0, err);
		}
	}
	printf("RESULT %d passed, %d failed\n", passed, failed);
	FreeLibrary(api.module);
	return failed > 255 ? 255 : failed;
}
