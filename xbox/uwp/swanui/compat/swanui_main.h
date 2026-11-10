/*
 * SwanStation Xbox - C ABI between the frontend's main.c/util.c and the host glue
 * (port-notes/specs/S1-integration-threading.md section 7.4, owner S1).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef SWANUI_MAIN_H
#define SWANUI_MAIN_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool swanui_main_init(int argc, char **argv);   /* main.c: everything before the loop; false if plat_init failed */
bool swanui_main_frame(void);                   /* main.c: one loop pass; false once the user quit */
void swanui_main_shutdown(void);                /* main.c: everything after the loop */
void swanui_main_suspend(void);                 /* main.c: save settings, stats, quick-resume state */
bool swanui_main_launch(const char *path);      /* main.c: --rom at run time (shelf only); path uses '/' (S0 D19) */
bool swanui_main_wants_emulation(void);         /* main.c: SCREEN_GAME or the Settings preview */
bool swanui_main_has_game(void);                /* main.c: app.game != NULL */
void swanui_main_set_focus(bool focus);         /* main.c: the window gained/lost the focus (the kiosk pauses without it) */
void swanui_main_lost_game(const char *message);/* main.c: app_stop_game() + toast */
void swanui_host_log(const char *line);         /* bridge/swanui_glue.cpp: mirror into the DuckStation log */
#ifdef __cplusplus
}
#endif
#endif
