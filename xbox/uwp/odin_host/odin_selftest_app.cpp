// Odin (experimental) shell: the in-app selftest, the same checks as overlay\selftest\odin_selftest.c (the desktop
// driver of M1, 29 checks: 25 ABI checks and the core's four selftests jit, board, describe, bios). It runs when
// LocalState\selftest.txt exists, before the real odin_init, and logs each PASS or FAIL line to odin-shell.log.
// No game, ROM or BIOS is needed or looked for. SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <cstring>
#include <string>

#include "odin_core_api.h"

extern "C" {
#include "compat/swanui_systems.h"
}

// The frontend's brokered folder creation (compat/swanui_compat.c).
extern "C" int swanui_mkdir_u8(const char *path, int mode);

namespace
{

int g_passed = 0;
int g_failed = 0;

void smoke_log(void *ctx, int32_t level, const char *text)
{
    (void)ctx;
    odin_log("selftest core %s: %s", level == ODIN_LOG_ERROR ? "error" : level == ODIN_LOG_WARN ? "warn" : "info",
             text ? text : "");
}

void verdict(const char *name, bool ok, const char *detail)
{
    const bool has_detail = detail && *detail;
    if (ok)
    {
        ++g_passed;
        odin_log("selftest: PASS %s%s%s", name, has_detail ? ": " : "", has_detail ? detail : "");
    }
    else
    {
        ++g_failed;
        odin_log("selftest: FAIL %s%s%s", name, has_detail ? ": " : "", has_detail ? detail : "");
    }
}

void make_folder(const std::string &path)
{
    if (swanui_mkdir_u8(path.c_str(), 0) != 0)
        odin_log("selftest: folder %s not created (it may exist)", path.c_str());
}

// The ABI checks (the driver's abi_smoke): init, catalogue, checks, loads that must fail, options, pads, states.
void abi_smoke(const std::string &data, const std::string &fixtures)
{
    char err[512] = "", report[1024] = "", text[64] = "";
    const std::string empty = fixtures + "/empty";
    OdinInit init;
    OdinCatalogEntry entry;
    OdinSetStatus status;
    OdinPad pads[2];
    uint32_t w = 9, h = 9, pitch = 9, fresh = 9;
    int32_t n;

    // ABI version first
    verdict("abi version", g_odin.abi() == ODIN_ABI, "");

    // init with the DLL's own file functions (io NULL, as the driver does) and a data folder
    memset(&init, 0, sizeof(init));
    init.abi = ODIN_ABI;
    init.install_dir = "";
    init.data_root = data.c_str();
    init.callbacks.ctx = nullptr;
    init.callbacks.log = smoke_log;
    n = g_odin.init(&init, err, sizeof(err));
    verdict("odin_init", n == ODIN_OK, n == ODIN_OK ? "" : err);
    if (n != ODIN_OK)
        return;
    // a second init is a no-op
    verdict("odin_init twice", g_odin.init(&init, err, sizeof(err)) == ODIN_OK, "");

    // the catalogue: 14 games (8 Hikaru, 6 Gaelco) and the five shared sets
    {
        int32_t count = g_odin.catalog_count(), games = 0, hikaru = 0, gaelco = 0, support = 0;
        bool ok_fields = true;
        for (int32_t i = 0; i < count; i++)
        {
            if (g_odin.catalog_get(i, &entry) != ODIN_OK)
            {
                ok_fields = false;
                continue;
            }
            if (entry.is_support)
            {
                support++;
                continue;
            }
            games++;
            if (strcmp(entry.machine, "hikaru") == 0)
                hikaru++;
            else if (strcmp(entry.machine, "gaelco") == 0)
                gaelco++;
        }
        snprintf(text, sizeof(text), "%d entries: %d games (%d hikaru, %d gaelco), %d support", (int)count, (int)games,
                 (int)hikaru, (int)gaelco, (int)support);
        verdict("catalogue counts", ok_fields && games == 14 && hikaru == 8 && gaelco == 6 && support == 5, text);
    }
    {
        int32_t airtrix = g_odin.catalog_find("airtrix"), airtrixo = g_odin.catalog_find("airtrixo");
        bool ok = airtrix >= 0 && airtrixo >= 0 && g_odin.catalog_get(airtrixo, &entry) == ODIN_OK &&
                  strcmp(entry.parent, "airtrix") == 0 && g_odin.catalog_find("nosuchgame") < 0;
        verdict("catalogue find and parent", ok, "airtrixo is a version of airtrix");
    }

    // a set that is not there: complete = no, the missing files are listed
    n = g_odin.check_set(empty.c_str(), empty.c_str(), "airtrix", &status, report, sizeof(report));
    snprintf(text, sizeof(text), "state %d, %u missing", (int)status.state, (unsigned)status.missing);
    verdict("check set on empty folders", n == ODIN_OK && status.state == 1 && status.missing > 0, text);
    verdict("check set lists the files", n == ODIN_OK && strstr(report, "in hikaru.zip") != nullptr, "");
    n = g_odin.check_set(empty.c_str(), empty.c_str(), "nosuchgame", &status, report, sizeof(report));
    verdict("check unknown game", n == ODIN_E_UNKNOWN_GAME, "");

    // loading a missing set: the shared BIOS is named, nothing is loaded
    n = g_odin.load("airtrix", empty.c_str(), empty.c_str(), "", err, sizeof(err));
    verdict("load with no BIOS and no set", n == ODIN_E_BIOS_MISSING && strncmp(err, "BIOS missing", 12) == 0, err);
    n = g_odin.load("nosuchgame", empty.c_str(), empty.c_str(), "", err, sizeof(err));
    verdict("load unknown game", n == ODIN_E_UNKNOWN_GAME, "");
    n = g_odin.load("hikaru", empty.c_str(), empty.c_str(), "", err, sizeof(err));
    verdict("load a shared set is not a game", n == ODIN_E_UNKNOWN_GAME, "");
    n = g_odin.load("", empty.c_str(), empty.c_str(), "", err, sizeof(err));
    verdict("load with no name", n == ODIN_E_ARG, "");

    // with nothing loaded: unload, flush, reset, frame and states say so
    verdict("unload with nothing loaded", g_odin.unload(err, sizeof(err)) == ODIN_E_NOT_LOADED, "");
    verdict("flush with nothing loaded", g_odin.flush_saves(err, sizeof(err)) == ODIN_E_NOT_LOADED, "");
    verdict("reset with nothing loaded", g_odin.reset(err, sizeof(err)) == ODIN_E_NOT_LOADED, "");
    n = g_odin.frame(&w, &h, &pitch, &fresh);
    verdict("frame with nothing loaded", n == ODIN_E_NOT_LOADED && w == 0 && h == 0 && fresh == 0, "");

    // the pads take a snapshot at any time
    memset(pads, 0, sizeof(pads));
    pads[0].connected = 1;
    pads[0].buttons = ODIN_PAD_A | ODIN_PAD_START;
    g_odin.set_pads(pads, 2);
    verdict("set pads", true, "");

    // options: range and key checks
    verdict("option scale 2", g_odin.option_set("scale", "2") == ODIN_OK, "");
    n = g_odin.option_get("scale", text, sizeof(text));
    verdict("option scale reads back", n > 0 && strcmp(text, "2") == 0, text);
    verdict("option scale 7 refused", g_odin.option_set("scale", "7") == ODIN_E_OPTION, "");
    verdict("option anisotropy 3 refused", g_odin.option_set("anisotropy", "3") == ODIN_E_OPTION, "");
    verdict("option unknown key refused", g_odin.option_set("nosuchkey", "1") == ODIN_E_OPTION, "");
    verdict("option default", g_odin.option_get("volume", text, sizeof(text)) > 0 && strcmp(text, "100") == 0, text);

    // the states without a game
    verdict("save state with nothing loaded", g_odin.save_state("nothing.odinstate", err, sizeof(err)) == ODIN_E_NOT_LOADED, "");
    verdict("load state with nothing loaded", g_odin.load_state("nothing.odinstate", err, sizeof(err)) == ODIN_E_NOT_LOADED, "");

    g_odin.shutdown();
}

// One call of the core's selftest under a structured exception handler: a fault inside odin_core.dll (seen with the
// JIT's executable memory refused in the app) is logged as a FAIL of that test, and the shell and the other tests go on.
// No C++ object with a destructor in this function (MSVC rejects __try there).
int32_t guarded_selftest(const char *name, char *report, size_t cap, unsigned long *fault_code)
{
    int32_t n = ODIN_E_INTERNAL;
    *fault_code = 0;
    __try
    {
        n = g_odin.selftest(name, report, cap);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION ||
                      GetExceptionCode() == EXCEPTION_INT_DIVIDE_BY_ZERO || GetExceptionCode() == EXCEPTION_STACK_OVERFLOW ||
                      GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                  ? EXCEPTION_EXECUTE_HANDLER
                  : EXCEPTION_CONTINUE_SEARCH)
    {
        *fault_code = GetExceptionCode();
        n = ODIN_E_INTERNAL;
    }
    return n;
}

// The core's own selftests (the driver's core_selftests): each report is logged line by line.
void core_selftests(void)
{
    static const char *const names[] = {"jit", "board", "describe", "bios"};
    for (const char *name : names)
    {
        char report[8192] = "";
        unsigned long fault = 0;
        const int32_t n = guarded_selftest(name, report, sizeof(report), &fault);
        if (fault)
            odin_log("selftest %s: the core raised exception 0x%08lX (access violation is C0000005); the test is a FAIL",
                     name, fault);
        std::string text = report;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos)
                end = text.size();
            odin_log("selftest report %s: %s", name, text.substr(pos, end - pos).c_str());
            pos = end + 1;
        }
        std::string label = std::string("selftest ") + name;
        verdict(label.c_str(), n == ODIN_OK, "");
    }
}

} // namespace

int odin_app_selftest(const char *data_root)
{
    g_passed = 0;
    g_failed = 0;
    const std::string root = std::string(data_root ? data_root : "") + "/selftest";
    const std::string data = root + "/data";
    const std::string fixtures = root + "/fixtures";
    make_folder(root);
    make_folder(data);
    make_folder(fixtures);
    make_folder(fixtures + "/empty");
    odin_log("selftest: start (%s), 29 checks as the desktop driver", root.c_str());
    if (!g_odin_ready)
    {
        odin_log("selftest: the core is not loaded, nothing run");
        return 1;
    }
    abi_smoke(data, fixtures);
    core_selftests();
    odin_log("selftest RESULT %d passed, %d failed", g_passed, g_failed);
    return g_failed;
}
