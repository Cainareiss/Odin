/*
 * SwanStationPS5 - a PS1 jewel case in 3D (assets/psx_case_NTSC.obj), drawn with plat_draw_mesh.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ARES_UWP (3D box package, 2026-11-08): every system has its case. Disc systems keep the jewel case; cartridge (and
 * tape, floppy) systems stand in a box modelled here: a cardboard box, a plastic clamshell or a cassette case, with
 * per-system proportions, depth and spine colours (see ARES_BOXES_NOTES.md). The *_of functions take the game's system
 * id (compat/swanui_systems.h; NULL or "": the PS1 jewel case, as in the PS1 builds) and its cover (NULL: none, the
 * shape the system's box has without one); the older functions are the jewel case's.
 */
#ifndef SwanStationPS5_CASE3D_H
#define SwanStationPS5_CASE3D_H

#include "../platform/platform.h"

#include <stdbool.h>
#include <stdint.h>

/* Loads the model on first use; false when the file is missing or unreadable (callers fall back to flat covers). */
bool case3d_available(void);

/* Width / height of the case facing the camera. */
float case3d_aspect(void);
/* Thickness / height: the edge showing when the case is turned. */
float case3d_depth(void);

/* The turn (radians, either way) up to which only the case's front is in view: further, its side shows. */
float case3d_max_yaw(void);
/* The tilt (radians, either way: top away or towards the camera) up to which the back is still out of view. */
float case3d_max_pitch(void);

/* The launch animation of the case drawn next (case3d_draw takes it and forgets it): `open` (0..1) swings its lid open on
 * the hinge, `spin` (radians) turns the CD on its tray; `in_tray`: the CD stays flat on the tray, without lifting or growing. */
void case3d_launch(float open, float spin, bool in_tray);

/* Whether the cover gets light (its shading and the glare over the lid's plastic); off: the picture as it is. The rest of
 * the case and the CD keep theirs. Stays until changed. */
void case3d_lighting(bool on);

/* How dark the patch of a case's contact shadow on the floor is (a case with a reflection has it): 1 as it is, less is more
 * transparent, 0 none. Stays until changed. */
void case3d_floor_shadow(float strength);

/* The next case drawn lights its cover sheet like matte paper (plain diffuse light from the light point of the plastic's
 * glare: it darkens as it turns from the light, no shine): for the picture shown for games without a cover. A box drawn
 * with it keeps the shape it has without a cover and shows its system's label band across the top of the picture. */
void case3d_matte(bool on);

/* One case centred on (cx, cy), `h` pixels tall. `yaw` turns it about the vertical axis (radians, positive takes its
 * right edge away from the camera), `pitch` about the horizontal one (positive takes its top away). `cover` is the front art (NULL: a plain card). `tint` shades it (rgb) and fades
 * it (alpha), like the flat covers' tint. `reflection` adds the mirror image on the floor below. */
void case3d_draw(PlatTexture *cover, float cx, float cy, float h, float yaw, float pitch, uint32_t tint,
                 bool reflection);

/* ---- per system (ARES_UWP: every system has a case; elsewhere system is "" and these are the jewel case's) ---- */

enum Case3DKind
{
    CASE3D_JEWEL,     /* the PS1 jewel case of the model (PlayStation, PC Engine CD, Mega CD with a square cover) */
    CASE3D_CARDBOARD, /* a cardboard box (NES, SNES, Game Boy family, PC Engine, MSX...) */
    CASE3D_CLAMSHELL, /* a plastic clamshell with a clear sleeve (Mega Drive, Master System, Game Gear, 32X, Mega CD's tall case) */
    CASE3D_TAPE,      /* a cassette case (ZX Spectrum) */
    CASE3D_DISC_BOX   /* a plain jewel-sized box for a disc system when the model is missing */
};

/* The case a game of `system` with this cover stands in. The Mega CD's is its tall case when the cover is a tall one
 * (the US long box scans), else the jewel case. */
int case3d_kind_of(const char *system, const PlatTexture *cover);
/* Its launch plays case3d_launch's animation (the lid opens, the CD spins up in it): the jewel case of the model only. */
bool case3d_opens(const char *system, const PlatTexture *cover);
/* Width and thickness of its case against the height `h` given to case3d_draw_of (a handheld's box is shorter than h:
 * every case stands on the same floor, at the bottom of a jewel case h tall). */
float case3d_aspect_of(const char *system, const PlatTexture *cover);
float case3d_depth_of(const char *system, const PlatTexture *cover);
/* The same as case3d_draw for a game of `system` (its box, or the jewel case). */
void case3d_draw_of(const char *system, PlatTexture *cover, float cx, float cy, float h, float yaw, float pitch,
                    uint32_t tint, bool reflection);

#endif
