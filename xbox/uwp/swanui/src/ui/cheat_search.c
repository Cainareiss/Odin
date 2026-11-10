/*
 * SwanStationPS5 - Find a code: a cheat search over the PS1's 2 MB of RAM.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Start a search (a snapshot), play until the value changes, come back and
 * say how it changed; the candidates shrink each round. When few are left,
 * pick one and a value: it becomes a GameShark code in the game's own list
 * (cheats/mine/<game id>.txt), 30AAAAAA 00VV for a byte, 80AAAAAA VVVV for
 * two bytes.
 *
 * ARES_UWP: every system whose main RAM the host's cheat engine reaches (compat/swanui_ares_cheats.h): the RAM, its
 * byte order and the code in the system's own format (Pro Action Replay, GameShark, Action Replay, Code Breaker...)
 * come from the host.
 */
#include "../app.h"
#include "../cheats.h"
#include "../core/host.h"
#include "../i18n.h"
#include "../platform/platform.h"
#include "draw.h"
#include "icons.h"
#include "libretro.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(ARES_UWP)
#include "swanui_ares_cheats.h"
#endif

#define RAM_SIZE (2 * 1024 * 1024)
#define SHOW_MAX 40

enum Filter
{
    F_INCREASED,
    F_DECREASED,
    F_UNCHANGED,
    F_CHANGED,
    F_EQUALS,
    F_COUNT
};

static struct
{
    const Game *game;     /* the search belongs to this game */
    bool running;
    int size;             /* 1 or 2 bytes */
    size_t ram;           /* bytes of RAM searched */
    bool big;             /* two-byte values high byte first (ARES_UWP: the Mega Drive) */
    uint8_t *prev;        /* RAM at the last step */
    uint8_t *now;         /* RAM now (narrow) */
    uint8_t *alive;       /* one bit per address */
    int count;
    int cursor;           /* filter row, or result row */
    int equals;           /* the value for "Equals" */
    int lock;             /* the value a picked address is locked to */
    int results[SHOW_MAX], result_count;
    bool picking;         /* choosing the value for results[cursor] */
} K;

/* ---------------------------------------------------------------- the RAM */

static size_t ram_size(void)
{
#if defined(ARES_UWP)
    return ares_cheat_ram_size();
#else
    const uint8_t *ram = host_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    return ram && host_memory_size(RETRO_MEMORY_SYSTEM_RAM) >= RAM_SIZE ? RAM_SIZE : 0;
#endif
}

static bool ram_read(uint8_t *dst, size_t offset, size_t size)
{
#if defined(ARES_UWP)
    return ares_cheat_ram_read(dst, offset, size);
#else
    const uint8_t *ram = host_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    if (!ram || offset + size > RAM_SIZE)
        return false;
    memcpy(dst, ram + offset, size);
    return true;
#endif
}

static unsigned value_at(const uint8_t *ram, int a)
{
    if (K.size != 2)
        return ram[a];
    return K.big ? (unsigned)(ram[a] << 8 | ram[a + 1]) : (unsigned)(ram[a] | ram[a + 1] << 8);
}

/* the value at a now (false: the RAM can't be read) */
static bool value_now(int a, unsigned *v)
{
    uint8_t b[2];
    if (!ram_read(b, (size_t)a, (size_t)K.size))
        return false;
    *v = K.size == 2 ? (K.big ? (unsigned)(b[0] << 8 | b[1]) : (unsigned)(b[0] | b[1] << 8)) : b[0];
    return true;
}

/* the address as the system shows it */
static void address_text(int a, char *out, size_t size)
{
#if defined(ARES_UWP)
    char code[64];
    if (ares_cheat_ram_code((size_t)a, 1, 0, code, sizeof(code), out, size) && out[0])
        return;
#endif
    snprintf(out, size, "%06X", (unsigned)a);
}

static bool alive(int a)
{
    return K.alive[a >> 3] & (1u << (a & 7));
}

static void collect(void)
{
    K.result_count = 0;
    for (int a = 0; (size_t)(a + K.size) <= K.ram && K.result_count < SHOW_MAX; a += K.size)
        if (alive(a))
            K.results[K.result_count++] = a;
}

static void start(int size)
{
    size_t n = ram_size();
    if (!n)
    {
        app_toast("This game's memory can't be searched");
        return;
    }
    if (n != K.ram)
    {
        free(K.prev);
        free(K.now);
        free(K.alive);
        K.prev = malloc(n);
        K.now = malloc(n);
        K.alive = malloc(n / 8 + 1);
        K.ram = n;
    }
    if (!K.prev || !K.now || !K.alive)
    {
        free(K.prev);
        free(K.now);
        free(K.alive);
        K.prev = K.now = K.alive = NULL;
        K.ram = 0;
        return;
    }
    if (!ram_read(K.prev, 0, n))
    {
        app_toast("This game's memory can't be searched");
        return;
    }
#if defined(ARES_UWP)
    K.big = ares_cheat_ram_big_endian();
#else
    K.big = false;
#endif
    memset(K.alive, 0, n / 8 + 1);
    K.count = 0;
    for (int a = 0; (size_t)(a + size) <= n; a += size)
    {
        K.alive[a >> 3] |= (uint8_t)(1u << (a & 7));
        ++K.count;
    }
    K.size = size;
    K.running = true;
    K.game = app.game;
    K.cursor = 0;
    K.picking = false;
    collect();
}

static void narrow(int filter)
{
    if (!K.now || !ram_read(K.now, 0, K.ram))
        return;
    int left = 0;
    for (int a = 0; (size_t)(a + K.size) <= K.ram; a += K.size)
    {
        if (!alive(a))
            continue;
        unsigned now = value_at(K.now, a), before = value_at(K.prev, a);
        bool keep = filter == F_INCREASED   ? now > before
                    : filter == F_DECREASED ? now < before
                    : filter == F_UNCHANGED ? now == before
                    : filter == F_CHANGED   ? now != before
                                            : now == (unsigned)K.equals;
        if (keep)
            ++left;
        else
            K.alive[a >> 3] &= (uint8_t)~(1u << (a & 7));
    }
    uint8_t *swap = K.prev;
    K.prev = K.now;
    K.now = swap;
    K.count = left;
    K.cursor = 0;
    collect();
    char msg[64];
    snprintf(msg, sizeof(msg), tr(left == 1 ? "%d address left" : "%d addresses left"), left);
    app_toast(msg);
}

static void save_code(int address)
{
    char code[96], desc[80], where[32];
    unsigned v = (unsigned)K.lock & (K.size == 2 ? 0xffffu : 0xffu);
#if defined(ARES_UWP)
    if (!ares_cheat_ram_code((size_t)address, K.size, v, code, sizeof(code), where, sizeof(where)))
    {
        app_toast("Could not save the code");
        return;
    }
#else
    if (K.size == 2)
        snprintf(code, sizeof(code), "80%06X %04X", (unsigned)address, v);
    else
        snprintf(code, sizeof(code), "30%06X 00%02X", (unsigned)address, v);
    snprintf(where, sizeof(where), "%06X", (unsigned)address);
#endif
    /* the translated text (i18n.c) takes the address as a number: the system's address from the host */
    snprintf(desc, sizeof(desc), tr("My code: %06X = %u"), (unsigned)strtoul(where, NULL, 16), v);
    if (cheats_add_user(&app.cheats, app.game, app.paths.cheats, desc, code))
    {
        cheats_apply(&app.cheats);
        app_toast("Saved and switched on: see Cheats");
        sfx_play(SFX_SELECT);
    }
    else
        app_toast("Could not save the code");
}

void cheat_search_open(void)
{
    if (K.game != app.game || ram_size() != K.ram)
        K.running = false;
    K.cursor = 0;
    K.picking = false;
    if (K.running)
        collect();
    app.screen = SCREEN_CHEAT_SEARCH;
}

void cheat_search_screen(uint32_t pressed)
{
    bool results = K.running && K.count <= SHOW_MAX && K.count > 0;
    int rows = !K.running ? 2 : results ? K.result_count + F_COUNT : F_COUNT;
    if (pressed & BIT(BTN_CIRCLE))
    {
        sfx_play(SFX_BACK);
        if (K.picking)
            K.picking = false;
        else
            app.screen = SCREEN_CHEATS;
        return;
    }
    if (K.picking)
    {
        int step = (pressed & (BIT(BTN_L1) | BIT(BTN_R1))) ? 10 : 1;
        if (pressed & (BIT(BTN_RIGHT) | BIT(BTN_R1)))
            K.lock += step;
        if (pressed & (BIT(BTN_LEFT) | BIT(BTN_L1)))
            K.lock -= step;
        int max = K.size == 2 ? 65535 : 255;
        K.lock = K.lock < 0 ? 0 : K.lock > max ? max : K.lock;
        if (pressed & BIT(BTN_CROSS))
        {
            save_code(K.results[K.cursor - F_COUNT]);
            K.picking = false;
        }
    }
    else
    {
        int before = K.cursor;
        if (pressed & BIT(BTN_UP))
            K.cursor = (K.cursor + rows - 1) % rows;
        if (pressed & BIT(BTN_DOWN))
            K.cursor = (K.cursor + 1) % rows;
        if (K.cursor != before)
            sfx_play(SFX_CLICK);
        if (K.running && K.cursor == F_EQUALS && (pressed & (BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_L1) | BIT(BTN_R1))))
        {
            int step = (pressed & (BIT(BTN_L1) | BIT(BTN_R1))) ? 10 : 1;
            K.equals += (pressed & (BIT(BTN_RIGHT) | BIT(BTN_R1))) ? step : -step;
            int max = K.size == 2 ? 65535 : 255;
            K.equals = K.equals < 0 ? 0 : K.equals > max ? max : K.equals;
        }
        if (pressed & BIT(BTN_CROSS))
        {
            sfx_play(SFX_SELECT);
            if (!K.running)
                start(K.cursor == 0 ? 1 : 2);
            else if (K.cursor < F_COUNT)
                narrow(K.cursor);
            else
            {
                unsigned v;
                if (value_now(K.results[K.cursor - F_COUNT], &v))
                {
                    K.lock = (int)v;
                    K.picking = true;
                }
            }
        }
        if ((pressed & BIT(BTN_SQUARE)) && K.running)
        {
            K.running = false;
            K.cursor = 0;
            app_toast("Search cleared");
        }
        if ((pressed & BIT(BTN_TRIANGLE)) && K.running)
        {
            sfx_play(SFX_BACK);
            app.screen = SCREEN_GAME; /* play until the value changes */
            return;
        }
    }

    app_draw_game(40);
    draw_rect(0, 0, plat_width(), plat_height(), TH_SCRIM);
    text_draw(TH_MARGIN, 40, 44, FONT_BOLD, TH_TEXT, ALIGN_LEFT, tr("Find a code"));
    char sub[160];
    if (!K.running)
        str_copy(sub, sizeof(sub), tr("Start, play until the value changes (lives, money...), come back and say how it changed."));
    else
        snprintf(sub, sizeof(sub), tr(K.count == 1 ? "%d address left: play, then say how the value changed."
                                                   : "%d addresses left: play, then say how the value changed."),
                 K.count);
    text_draw_fit(TH_MARGIN + 2, 100, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, plat_width() - 2 * TH_MARGIN, sub);

    const float x = 260, w = plat_width() - 520.0f, top = 170, row_h = 62;
    draw_rrect(x - 16, top - 16, w + 32, plat_height() - top - 110, TH_RADIUS, TH_CARD);
    static const char *const starts[] = {"New search: 1 byte (0 to 255)", "New search: 2 bytes (0 to 65535)"};
    static const char *const filters[F_COUNT] = {"Increased", "Decreased", "Unchanged", "Changed", "Equals"};
    int visible = (int)((plat_height() - top - 140) / row_h);
    int first = K.cursor >= visible ? K.cursor - visible + 1 : 0;
    for (int i = first; i < rows && i < first + visible; ++i)
    {
        float y = top + (i - first) * row_h;
        bool on = i == K.cursor;
        if (on)
            draw_rrect(x, y, w, row_h - 8, TH_RADIUS_SMALL, TH_ROW_SELECTED);
        char label[96], value[64] = "";
        if (!K.running)
            str_copy(label, sizeof(label), tr(starts[i]));
        else if (i < F_COUNT)
        {
            str_copy(label, sizeof(label), tr(filters[i]));
            if (i == F_EQUALS)
                snprintf(value, sizeof(value), "< %d >", K.equals);
        }
        else
        {
            int a = K.results[i - F_COUNT];
            unsigned v;
            address_text(a, label, sizeof(label));
            if (on && K.picking)
                snprintf(value, sizeof(value), tr("lock to < %d >"), K.lock);
            else if (value_now(a, &v))
                snprintf(value, sizeof(value), tr("now %u"), v);
        }
        text_draw(x + 24, y + 14, 24, on ? FONT_BOLD : FONT_REGULAR, on ? TH_TEXT : TH_TEXT_SOFT, ALIGN_LEFT, label);
        if (value[0])
            text_draw(x + w - 24, y + 14, 24, FONT_REGULAR, TH_FOCUS, ALIGN_RIGHT, value);
        if (K.running && i == F_COUNT - 1 && results)
            draw_rect(x + 16, y + row_h - 3, w - 32, 2, TH_DIVIDER);
    }

    if (K.picking)
    {
        static const int g[] = {GLYPH_LEFT, GLYPH_RIGHT, GLYPH_CROSS, GLYPH_CIRCLE};
        static const char *const l[] = {"-1", "+1", "Save the code", "Back"};
        app_draw_hints(g, l, 4, NULL);
        const HintCombo right[1] = {{{GLYPH_L1, GLYPH_R1}, 2, '/', "-10 / +10"}};
        app_draw_hints_right(right, 1);
    }
    else if (K.running)
    {
        static const int g[] = {GLYPH_CROSS, GLYPH_TRIANGLE, GLYPH_SQUARE, GLYPH_CIRCLE};
        static const char *const l[] = {"Choose", "Play", "Clear", "Back"};
        app_draw_hints(g, l, 4, NULL);
    }
    else
    {
        static const int g[] = {GLYPH_CROSS, GLYPH_CIRCLE};
        static const char *const l[] = {"Start", "Back"};
        app_draw_hints(g, l, 2, NULL);
    }
    app_draw_toast();
}
