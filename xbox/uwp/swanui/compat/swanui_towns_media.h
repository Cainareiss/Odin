/* Tsugaru UWP port (FM Towns) - the media of a game: a CD-ROM image or a 3.5-inch floppy image.
 * The shelf lists both; the host (host_load) starts a CD with -CD and a floppy with -FD0, and a CD game may bring a
 * floppy along (-FD0, the boot/save disk many Towns CD games ask for). Defined in compat/swanui_towns_media.c.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWANUI_TOWNS_MEDIA_H
#define SWANUI_TOWNS_MEDIA_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum
{
    TOWNS_MEDIA_CD = 0, /* .cue .iso .mds (.bin/.mdf of a sheet), or a bigger raw .bin/.img */
    TOWNS_MEDIA_FD = 1  /* .d77 .d88, or a raw .bin/.img of at most 2 MB (a 640K to 1.44M floppy) */
};

/* The media of a game file by its name; a raw .bin/.img by its size (the file is looked at). A .m3u playlist has the
 * media of the first image it lists. Unknown or unreadable: TOWNS_MEDIA_CD. Any thread. */
int towns_media_of_path(const char *path);

/* A game that starts from a CD may come with a floppy image beside it. Returns 1 and the floppy's full path in `out`
 * when there is one: an image whose name starts with the CD image's name, else the only floppy image of a folder that
 * holds a single CD game. 0 otherwise (and for a game that is a floppy itself). Any thread. */
int towns_companion_fd(const char *game_path, char *out, size_t size);

/* "FMT-1A2B3C4D": a stable id for a game, from its title (no PS1 serial exists for the FM Towns). */
void towns_serial_from_title(const char *title, char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif /* SWANUI_TOWNS_MEDIA_H */
