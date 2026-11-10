/* Tsugaru (FM Towns, from the SwanStation Xbox port) - the library's disc filter. New file, SWANUI_XBOX only (step 10, 2026-10-31).
 * A folder with games lying loose in it lists a .cue and the .bin it describes as two discs, and a .cue whose .bin is
 * not there (only a .bin.ecm is) as a disc that cannot start. library.c asks here which entries to leave out.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWANUI_XBOX_LIBRARY_H
#define SWANUI_XBOX_LIBRARY_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* names: `count` entries of `stride` bytes each (library.c's NameBuf), the files of `dir`. hidden[i] becomes 1 for
 * - a .bin/.img/.iso/.ecm/.sub/.mdf that a usable .cue/.ccd/.mds in the same folder refers to (the sheet is the disc),
 * - a .cue/.ccd/.mds whose files are not all beside it (it cannot be played; the images stand on their own).
 * Other entries keep their value. The sheets are read with the compat fopen (brokered on the console). */
void swanui_xbox_hide_sheet_parts(const char *dir, const char *names, size_t stride, int count, unsigned char *hidden);

#ifdef __cplusplus
}
#endif
#endif /* SWANUI_XBOX_LIBRARY_H */
