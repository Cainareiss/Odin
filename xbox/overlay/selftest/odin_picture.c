/*
 * odin_picture.exe: the PC driver of the picture path (M3). A desktop console
 * program: it loads Dozen's ICD (vulkan_dzn.dll) the way the app does
 * (vk_icdNegotiateLoaderICDInterfaceVersion, then vk_icdGetInstanceProcAddr),
 * hands the entry to odin_core.dll (odin_set_vulkan_icd), and runs
 * odin_selftest_picture. The knob (DZN_FORCE_FL11_0) is set before Dozen loads,
 * as the app sets it.
 *
 *   odin_picture.exe <odin_core.dll> <data folder> <vulkan_dzn.dll> <frames> [programs list | -] [knob: unset|1|2]
 *
 * Prints the report of the core and RESULT. Exit status 0 when the picture path
 * passed. GPL-3.0-or-later.
 */

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "odin_c.h"

typedef int32_t (*abi_fn)(void);
typedef int32_t (*init_fn)(const OdinInit *, char *, size_t);
typedef void (*shutdown_fn)(void);
typedef void (*set_icd_fn)(void *);
typedef int32_t (*picture_fn)(const char *, uint32_t, char *, size_t);
typedef int32_t (*negotiate_fn)(uint32_t *);
typedef void *(*gipa_fn)(void *, const char *);

static void
core_log(void *ctx, int32_t level, const char *text)
{
	(void)ctx;
	printf("  [core %s] %s\n", level == ODIN_LOG_ERROR ? "error" : level == ODIN_LOG_WARN ? "warn" : "info", text);
	fflush(stdout);
}

/* A crash in the core prints where it is: the address, the kind of access and the offset in odin_core.dll (to map
 * with llvm-nm on the -O1 build, which keeps its symbols) */
static HMODULE g_core_module = NULL;

static LONG WINAPI
on_crash(EXCEPTION_POINTERS *info)
{
	const EXCEPTION_RECORD *rec = info->ExceptionRecord;
	const char *core_name = "";

	printf("CRASH code 0x%08lx at %p", (unsigned long)rec->ExceptionCode, rec->ExceptionAddress);
	if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
		printf(", %s of %p", rec->ExceptionInformation[0] ? "write" : "read", (void *)rec->ExceptionInformation[1]);
	if (g_core_module && (const char *)rec->ExceptionAddress >= (const char *)g_core_module) {
		printf(", odin_core.dll offset 0x%llx (base %p)",
		       (unsigned long long)((const char *)rec->ExceptionAddress - (const char *)g_core_module),
		       (void *)g_core_module);
		core_name = " in odin_core.dll";
	}
	printf("%s\n", core_name);
	fflush(stdout);
	return EXCEPTION_EXECUTE_HANDLER;
}

static void *
need(HMODULE m, const char *name)
{
	void *p = (void *)GetProcAddress(m, name);

	if (!p)
		printf("FAIL resolve %s (GetLastError %lu)\n", name, GetLastError());
	return p;
}

int
main(int argc, char **argv)
{
	if (argc < 5) {
		printf("usage: odin_picture <odin_core.dll> <data folder> <vulkan_dzn.dll> <frames> [programs|-] [knob]\n");
		return 2;
	}
	const char *core_path = argv[1], *data = argv[2], *icd_path = argv[3];
	const uint32_t frames = (uint32_t)strtoul(argv[4], NULL, 10);
	const char *list = argc > 5 && strcmp(argv[5], "-") != 0 ? argv[5] : "";
	const char *knob = argc > 6 ? argv[6] : "unset";
	int failed = 0;

	printf("odin_picture: core %s, icd %s, frames %u, list '%s', knob %s\n", core_path, icd_path,
	       (unsigned)frames, list, knob);
	if (strcmp(knob, "unset") != 0)
		SetEnvironmentVariableA("DZN_FORCE_FL11_0", knob);
	else
		SetEnvironmentVariableA("DZN_FORCE_FL11_0", NULL);

	/* the ICD first (the app does the same: LoadPackagedLibrary + negotiate + the entry) */
	HMODULE icd = LoadLibraryA(icd_path);
	if (!icd) {
		printf("RESULT 0 passed, 1 failed (the ICD did not load: %lu)\n", GetLastError());
		return 1;
	}
	negotiate_fn negotiate = (negotiate_fn)need(icd, "vk_icdNegotiateLoaderICDInterfaceVersion");
	gipa_fn gipa = (gipa_fn)need(icd, "vk_icdGetInstanceProcAddr");
	if (!negotiate || !gipa) {
		printf("RESULT 0 passed, 1 failed\n");
		return 1;
	}
	uint32_t version = 5;
	const int32_t nego = (int32_t)negotiate(&version);
	printf("icd: vk_icdNegotiateLoaderICDInterfaceVersion -> %d, version %u\n", nego, (unsigned)version);

	HMODULE core = LoadLibraryA(core_path);
	if (!core) {
		printf("RESULT 0 passed, 1 failed (the core did not load: %lu)\n", GetLastError());
		return 1;
	}
	g_core_module = core;
	SetUnhandledExceptionFilter(on_crash);
	abi_fn abi = (abi_fn)need(core, "odin_abi");
	init_fn init = (init_fn)need(core, "odin_init");
	shutdown_fn shutdown = (shutdown_fn)need(core, "odin_shutdown");
	set_icd_fn set_icd = (set_icd_fn)need(core, "odin_set_vulkan_icd");
	picture_fn picture = (picture_fn)need(core, "odin_selftest_picture");
	if (!abi || !init || !shutdown || !set_icd || !picture) {
		printf("RESULT 0 passed, 1 failed\n");
		return 1;
	}
	printf("odin_abi: %d (the header says %d)\n", (int)abi(), ODIN_ABI);
	if (abi() != ODIN_ABI)
		failed++;

	set_icd((void *)gipa);

	OdinInit init_block;
	char err[512] = "", report[8192] = "";

	memset(&init_block, 0, sizeof(init_block));
	init_block.abi = ODIN_ABI;
	init_block.install_dir = "C:/claude-work/odinuwp/odin";
	init_block.data_root = data;
	init_block.callbacks.log = core_log;
	if (init(&init_block, err, sizeof(err)) != ODIN_OK) {
		printf("FAIL odin_init: %s\n", err);
		printf("RESULT 0 passed, 1 failed\n");
		return 1;
	}
	const int32_t status = picture(list, frames, report, sizeof(report));

	printf("---- report (status %d)\n%s----\n", (int)status, report);
	if (status != ODIN_OK)
		failed++;
	shutdown();
	printf("RESULT %s (status %d)\n", failed ? "FAIL" : "PASS", (int)status);
	return failed ? 1 : 0;
}
