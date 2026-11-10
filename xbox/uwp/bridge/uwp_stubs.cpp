// Tsugaru UWP port (copied from the Flamenco One xbox_stubs.cpp) - PS5-only services as neutral stubs: NFS, NTFS, disc import/play/watch, updater, phone remote,
// Vulkan probe (port-notes/specs/S4-services.md section 6).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The frontend files that implement these on the PS5 (nfs/, ntfs/, discimport.c, cdscsi.c, update.c, remote.c,
// platform/vk/vk_probe.c) are not compiled into swanui.lib (S0 D22). Every function the compiled frontend calls is
// defined here with the "not there" value, so the screens behave as on a PS5 with no NFS share, no NTFS disk, no USB
// disc drive, no update and the phone page off. The NTFS C++ functions and discplay_file/discplay_read are not
// defined: nothing compiled calls them.

#include <cstring>

// 3. frontend (S0 D24)
#include "swanui_cdecl.h"
#define Settings SwanSettings
extern "C" {
#include "src/SwanStationPS5.h"
#include "src/app.h"
#include "src/discimport.h"
#include "src/platform/vk/vk_probe.h"
#include "src/remote.h"
#include "src/update.h"
}
#undef Settings
// These three carry their own extern "C" guards and stay outside the block (S0 D24, S4 section 1).
#include "src/discplay.h"
#include "src/nfs/nfs.h"
#include "src/ntfs/ntfsfs.h"

namespace {
// The frontend translates this one (compat/swanui_xbox_i18n.c, S5 entry 83) when it reaches tr()/app_toast.
constexpr const char* NOT_AVAILABLE = "Not available on Xbox";

const DiscId s_zero_disc_id = {}; // discplay_id() is dereferenced by REF main.c, so never NULL

bool s_remote_loop_started = false; // set by the first remote_frame(), which only the main loop calls
} // namespace

//////////////////////////////////////////////////////////////////////////
// nfs/nfs.h: no NFS shares (S4 section 6.2)
//////////////////////////////////////////////////////////////////////////

// An empty list is accepted. Otherwise the first address is reported as "<address>\t<reason>", which
// settings_screen.c turns into "<address>: <tr(reason)>" and main.c only logs at start.
extern "C" bool nfs_set_shares(const char* list, char* error, size_t error_size)
{
  if (error && error_size > 0)
    error[0] = '\0';

  // first address: up to ';' or a new line, without the blanks around it
  const char* first = nullptr;
  size_t first_len = 0;
  for (const char* p = list ? list : ""; *p;)
  {
    while (*p == ';' || *p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')
      p++;
    const char* start = p;
    while (*p && *p != ';' && *p != '\r' && *p != '\n')
      p++;
    const char* end = p;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
      end--;
    if (end > start)
    {
      first = start;
      first_len = static_cast<size_t>(end - start);
      break;
    }
  }
  if (!first)
    return true;

  if (error && error_size > 0)
  {
    const size_t reason_len = std::strlen(NOT_AVAILABLE);
    const size_t room = (error_size > reason_len + 2) ? (error_size - reason_len - 2) : 0; // '\t' and '\0'
    const size_t len = (first_len < room) ? first_len : room;
    std::memcpy(error, first, len);
    error[len] = '\0';
    if (len + 1 + reason_len < error_size)
    {
      error[len] = '\t';
      std::memcpy(error + len + 1, NOT_AVAILABLE, reason_len + 1);
    }
  }
  SwanStationPS5_log("nfs: NFS shares are not available on Xbox");
  return false;
}

extern "C" int nfs_mount_all(int timeout_ms)
{
  (void)timeout_ms;
  return 0;
}

extern "C" int nfs_mount_points(char out[][NFS_PATH_MAX], int max)
{
  (void)out;
  (void)max;
  return 0;
}

extern "C" void nfs_mount_begin(int timeout_ms)
{
  (void)timeout_ms;
}

extern "C" bool nfs_mount_running(void)
{
  return false;
}

extern "C" bool nfs_mount_finished(void)
{
  return false;
}

extern "C" int nfs_first_failure(char* detail, size_t size)
{
  if (detail && size > 0)
    detail[0] = '\0';
  return NFS_FAIL_NONE;
}

extern "C" bool nfs_connection_lost(void)
{
  return false;
}

extern "C" void nfs_set_read_timeout(int seconds)
{
  (void)seconds;
}

extern "C" bool nfs_reconnecting(void)
{
  return false;
}

extern "C" int nfs_unmounted(void)
{
  return 0;
}

extern "C" void nfs_set_sbi_dir(const char* dir)
{
  (void)dir;
}

extern "C" void nfs_describe(char* out, size_t size)
{
  if (out && size > 0)
    out[0] = '\0';
}

//////////////////////////////////////////////////////////////////////////
// ntfs/ntfsfs.h: no NTFS volumes of its own, Windows mounts NTFS itself (S4 section 6.3)
//////////////////////////////////////////////////////////////////////////

extern "C" int ntfsfs_scan(void)
{
  return 0;
}

extern "C" int ntfsfs_mount_points(char out[][NTFSFS_PATH_MAX], int max)
{
  (void)out;
  (void)max;
  return 0;
}

extern "C" bool ntfsfs_connection_lost(void)
{
  return false;
}

extern "C" int ntfsfs_dirty_count(void)
{
  return 0;
}

//////////////////////////////////////////////////////////////////////////
// discimport.h: no disc drive (S4 section 6.4)
//////////////////////////////////////////////////////////////////////////

extern "C" bool discimport_start(const char* games_dir)
{
  (void)games_dir;
  return false;
}

extern "C" void discwatch_skip_present(void)
{
}

extern "C" DiscImportState discimport_state(void)
{
  return DI_IDLE;
}

extern "C" void discimport_ack(void)
{
}

extern "C" int discimport_permille(void)
{
  return 0;
}

extern "C" const char* discimport_title(void)
{
  return "";
}

extern "C" bool discimport_check_stall(void)
{
  return false;
}

extern "C" void discimport_cancel(void)
{
}

extern "C" int discimport_bad(void)
{
  return 0;
}

extern "C" const char* discimport_games_dir(void)
{
  return "";
}

extern "C" void discimport_cleanup(const char* games_dir)
{
  (void)games_dir;
}

extern "C" const char* discimport_serial(void)
{
  return "";
}

extern "C" const char* discimport_key(void)
{
  return "";
}

extern "C" struct PlatTexture* discimport_cover(void)
{
  return nullptr;
}

extern "C" int discimport_total_sectors(void)
{
  return 0;
}

extern "C" bool discimport_dumping(void)
{
  return false;
}

extern "C" bool discimport_cover_none(void)
{
  return false;
}

extern "C" void discwatch_set_active(bool active)
{
  (void)active;
}

// false and *out untouched: the "play or install this disc?" question never opens
extern "C" bool discwatch_take(DiscId* out)
{
  (void)out;
  return false;
}

extern "C" void discwatch_pause(bool paused)
{
  (void)paused;
}

// constant, so main.c never sees a "disc taken out" transition
extern "C" int discwatch_removed(void)
{
  return 0;
}

extern "C" bool discplay_start(void)
{
  return false;
}

extern "C" DiscPlayState discplay_state(void)
{
  return DP_IDLE;
}

extern "C" DiscImportState discplay_error(void)
{
  return DI_IDLE;
}

extern "C" const DiscId* discplay_id(void)
{
  return &s_zero_disc_id;
}

extern "C" const char* discplay_path(void)
{
  return "";
}

extern "C" int discplay_permille(void)
{
  return 0;
}

extern "C" bool discplay_lost(void)
{
  return false;
}

extern "C" void discplay_stop(void)
{
}

//////////////////////////////////////////////////////////////////////////
// update.h: an update is never available (S4 section 6.5)
//////////////////////////////////////////////////////////////////////////

extern "C" void update_check(void)
{
  SwanStationPS5_log("update: not available on Xbox");
}

extern "C" void update_install(void)
{
  SwanStationPS5_log("update: not available on Xbox");
}

extern "C" int update_state(void)
{
  return UPDATE_NONE;
}

extern "C" const char* update_version(void)
{
  return "";
}

extern "C" const char* update_message(void)
{
  return "";
}

//////////////////////////////////////////////////////////////////////////
// remote.h: the phone page is off (S4 section 6.6)
//////////////////////////////////////////////////////////////////////////

// Turning it on snaps the toggle back to Off (the settings screen then saves it). At start-up a stale remote=1 from
// a copied config is cleared silently; once the main loop runs, the player gets a toast.
extern "C" void remote_update(bool enabled)
{
  if (!enabled)
    return;

  app.global.remote = false;
  app.settings.remote = false;
  SwanStationPS5_log("remote: the phone page is not available on Xbox");
  if (s_remote_loop_started)
    app_toast(NOT_AVAILABLE);
}

extern "C" void remote_frame(void)
{
  s_remote_loop_started = true;
}

extern "C" const char* remote_address(void)
{
  return "";
}

//////////////////////////////////////////////////////////////////////////
// platform/vk/vk_probe.h (S4 section 6.7)
//////////////////////////////////////////////////////////////////////////

extern "C" void vk_probe(const char* root)
{
  (void)root;
  SwanStationPS5_log("Odin: see the credits file in the package");
}
