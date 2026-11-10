/*
 * SwanStationPS5 - settings: tabs down the left, grouped rows, a help panel, and
 * per-game settings (L2 / R2 switch between this game and all games).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../app.h"
#include "../config.h"
#include "../controls.h"
#include "../core/host.h"
#include "../drives.h"
#include "../i18n.h"
#include "../nfs/nfs.h"
#include "../profiles.h"
#include "../platform/platform.h"
#include "../platform/xbr.h"
#include "ra/achievements.h"
#include "../remote.h"
#include "../update.h"
#include "../../third_party/qrcodegen/qrcodegen.h"
#include "coverflow.h"
#include "draw.h"
#include "icons.h"
#include "sfx.h"
#include "text.h"
#include "theme.h"
#if defined(SWANUI_XBOX)
#include "swanui_xbox_ui.h" /* SWANUI_XBOX (S5): swanui_core_version() */
#include "swanui_xbox_folder.h" /* SWANUI_XBOX: the games folder chosen with the Xbox folder picker */
#endif
#if defined(ARES_UWP)
#include "swanui_ares.h" /* ARES_UWP: plat_set_hdr / plat_hdr_state, app_apply_hdr */
#include "swanui_sysopts.h" /* ARES_UWP: the options of each system (Systems tab, the system pages) */
#include "swanui_systems.h"
#endif

#include "stb_image.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- the rows */

enum Kind
{
    K_CHOICE,
    K_TOGGLE,
    K_ACTION,
    K_INFO,
};

enum Apply
{
    APPLY_NOW,
    APPLY_NEXT_GAME,
    APPLY_NEXT_LAUNCH,
};

enum Special
{
    SP_NONE,
    SP_LANGUAGE,
    SP_HARDCORE,
    SP_UNLOCK,
    SP_ACCOUNT,
    SP_BIOS,
    SP_BIOS_START,
    SP_GAMES,
    SP_DATA,
    SP_CREDITS,
    SP_VERSION,
    SP_REMAP,
    SP_RESCAN,
    SP_SOUND,
    SP_MEMCARDS,
    SP_REMOTE,
    SP_UPDATE,
    SP_HOTKEYS,
    SP_WHITELIST,
    SP_STATS,
    SP_THEME,
    SP_PLAYERS,
    SP_PROFILE,
    SP_WHO,
    SP_HDTEX, /* drives the two core options for texture packs */
    SP_USBFOLDER, /* the games folder on USB drives, typed */
    SP_NFSHARES,  /* the NFS servers that hold games, typed */
    SP_DRIVES,    /* the extended and USB drives found, with their free space */
#if defined(SWANUI_XBOX)
    SP_CORE, /* SWANUI_XBOX (S5): About > Emulation core: DuckStation's version */
#endif
    SP_HDR, /* ARES_UWP: Display > HDR: applied with plat_set_hdr, the display's state in the help */
    SP_SYSTEM,     /* ARES_UWP: Systems tab: opens that system's options (Row.base = its index in swanui_systems()) */
    SP_MENU_COMBO, /* ARES_UWP: Controls > Game menu: the combinations that open it (information) */
};

typedef struct
{
    const char *group; /* starts a new card with this caption */
    const char *name;
    const char *help;
    unsigned char kind, apply, special;
    bool global_only; /* the same for every game */
    int offset;       /* into Settings, -1 for specials */
    bool is_bool;
    const char *const *labels;
    int count, base;
    const unsigned char *order; /* the setting's value of each label, when the labels are not in the value's order */
    const int *ivalues; /* ARES_UWP: the same for values over 255 (nits): the value of each label */
} Row;

/* "Your profile" is for a signed-in account; without one the row signs in. */
static const char *row_name(const Row *r)
{
    return r->special == SP_PROFILE && !ra_user()[0] ? "Sign in" : r->name;
}

static const char *row_help(const Row *r)
{
    return r->special == SP_PROFILE && !ra_user()[0]
               ? "Sign in to RetroAchievements with your user name and password, typed on the PS5."
               : r->help;
}

#define INT_FIELD(f) offsetof(Settings, f), false
#define BOOL_FIELD(f) offsetof(Settings, f), true
#define NO_FIELD -1, false

static const char *const OFF_ON[] = {"Off", "On"};
static const char *const RENDERERS[] = {"GPU (Recommended)", "CPU"};
static const char *const INTERNAL[] = {"Native", "2x", "3x (720p)", "4x", "5x (1080p)", "6x (1440p)", "7x", "8x", "9x (4K)", "10x", "11x", "12x", "13x", "14x", "15x", "16x"};
static const char *const NFS_TIMEOUTS[] = {"5 s", "10 s", "15 s", "20 s", "30 s"};
static const char *const CD_SPEEDS[] = {"Off", "2x", "3x", "4x", "5x", "6x", "7x", "8x", "9x", "10x"};
/* unused while the Emulator row is commented out */
static const __attribute__((unused)) char *const UPSCALE[] = {"Off", "2x", "3x", "4x"};
static const char *const FILTERS[] = {"Sharp pixels", "Smooth pixels (Scale2x)", "xBR (smoothest)"};
static const char *const REGIONS[] = {"Auto", "NTSC-J (60 Hz)", "NTSC-U (60 Hz)", "PAL (50 Hz)"};
static const char *const PADS[] = {"Digital pad", "DualShock (analog)"};
/* the pre-rendered 3D box covers are no longer used: the covers are always flat
static const char *const COVER_STYLES[] = {"Flat", "3D box"}; */
static const char *const SOUND_STYLES[] = {"Soft", "Wood", "Pop", "Chime", "Classic", "Off"};
static const char *const VOLUMES[] = {"25%", "50%", "75%", "100%"};
static const char *const VIBRATIONS[] = {"Off", "25%", "50%", "75%", "100%"};
static const char *const STICK_MODES[] = {"Auto (digital games)", "Always", "Off"};
static const char *const ANALOG_COMBOS[] = {
    "{L1} + {R1} + {L3} + {R3}", "{L1} + {L2} + {R1} + {R2} + {Options} + {Touchpad}", "{L1} + {R1} + {Touchpad}",
    "{L1} + {R1} + {Options}", "{L1} + {R1} + {L3}", "{L1} + {R1} + {R3}", "{L2} + {R2} + {Touchpad}",
    "{L2} + {R2} + {Options}", "{L2} + {R2} + {L3}", "{L2} + {R2} + {R3}", "{L3} + {R3}"};
static const char *const MENU_COMBOS[] = {"{Touchpad} + {R3}", "{L3} + {R3}", "{Options} + {Touchpad}"};
static const char *const MENU_HOLDS[] = {"Immediate", "0.5 seconds", "1 second"};
static const char *const BACKGROUNDS[] = {"Dark", "Cover colour"};
static const char *const CRT_LEVELS[] = {"Off", "Light", "Strong"};
static const char *const BORDERS[] = {"Black", "Current background", "TV frame"};
static const char *const CROPS[] = {"Off", "A little", "More"};
static const char *const SHADERS[] = {"Off", "LCD3x", "NTSC 320px S-Video", "NTSC 320px Composite",
                                      "NTSC 256px S-Video", "NTSC 256px Composite", "CRT Lottes", "CRT Pi",
                                      "zfast LCD", "CRT Royale Fast", "CRT Royale", "CRT Newpixie",
                                      "CRT Hyllian NTSC", "CRT Mattias"};
static const unsigned char SHADER_ORDER[] = {0, 1, 5, 6, 7, 8, 9, 10, 12, 2, 13, 14, 15, 16};
static const char *const POPUP_STYLES[] = {"Banner", "Compact", "Big trophy"};
static const char *const BIOS_START_REGIONS[] = {"NTSC-U/C (US, Canada)", "NTSC-J (Japan)", "PAL (Europe, Australia)"};
static const char *const LIGHTBARS[] = {"System", "Player colours", "Game cover colour", "Analog mode"};
static const char *const PLAYERS[] = {"1 or 2", "Up to 4 (multitap)"};
static const char *const DEADZONES[] = {"Off", "5%", "10%", "15%", "20%"};
static const char *const RESPONSES[] = {"Normal", "Precise", "Quick"};
static const char *const TRIGGER_FEELS[] = {"Off", "Automatic", "Click", "Light click", "Firm", "Buzz"};
static const char *const RUMBLE_FEELS[] = {"Classic", "Soft", "Punchy", "Punchy, in the triggers too"};
static const char *const LIGHTGUNS[] = {"Automatic", "Off", "On"};
static const char *const __attribute__((unused)) OVERCLOCKS[] = {"Off", "A little", "A lot"};
static const char *const BRIGHTNESS[] = {"Darker", "Normal", "Brighter", "Brightest"};
static const char *const COLOURS[] = {"Natural", "Vivid", "Soft", "Warm", "Cool", "Black and white"};
static const char *const SHARPENS[] = {"Off", "Light", "Strong"};
static const char *const RUN_AHEADS[] = {"Off", "1 frame", "2 frames", "3 frames", "4 frames", "5 frames", "6 frames", "7 frames", "8 frames", "9 frames", "10 frames"};
static const char *const __attribute__((unused)) MSAA[] = {"Off", "2x", "4x", "8x", "16x"};
static const char *const __attribute__((unused)) TEXTURE_FILTERS[] = {"Off", "Bilinear", "xBR", "SABR", "JINC2", "3-point"};
static const char *const __attribute__((unused)) DEINTERLACERS[] = {"Weave", "Bob", "Motion-adaptive"};
static const char *const AUTOSAVES[] = {"Off", "Every 5 minutes", "Every 10 minutes", "Every 15 minutes"};
static const char *const FRAMEGENS[] = {"Off", "x2", "x4"};
static const char *const VIDEO_OUTS[] = {"60 Hz", "120 Hz"};
static const char *const SORTS[] = {"Title", "Recently played", "Most played", "Region"};
#if defined(ARES_UWP)
static const char *const HDR_MODES[] = {"Off", "Auto", "On"};
static const char *const HDR_PEAKS[] = {"400 nits", "600 nits", "800 nits", "1000 nits", "1500 nits", "2000 nits", "3000 nits", "4000 nits"};
static const int HDR_PEAK_VALUES[] = {400, 600, 800, 1000, 1500, 2000, 3000, 4000};
static const char *const HDR_PAPERS[] = {"80 nits", "100 nits", "150 nits", "200 nits", "250 nits", "300 nits", "400 nits", "500 nits"};
static const int HDR_PAPER_VALUES[] = {80, 100, 150, 200, 250, 300, 400, 500};
static const char *const HDR_BOOSTS[] = {"Off", "Subtle", "Normal", "Strong"};
#endif

static const Row DISPLAY[] = {
    /* "Upscale filter" and "Upscale" are CPU prescalers (Scale2x, xBR) for the software frames of the old core;
     * SwanStation draws on the GPU and never goes through them, so they did nothing. To bring them back, restore
     * these two rows and the "Picture" group header on the first one.
    {"Picture", "Upscale filter", "How pixels are smoothed when the picture is enlarged.", K_CHOICE,
     APPLY_NOW, SP_NONE, false, INT_FIELD(upscale_filter), FILTERS, 3, 0},
    {NULL, "Upscale", "Enlarges the picture before it is scaled to the screen.", K_CHOICE, APPLY_NOW,
     SP_NONE, false, INT_FIELD(upscale), UPSCALE, 4, 1},
    */
    {"Picture", "Shader", "Gives the picture the look of a CRT TV, a handheld LCD or the analog NTSC signal. The most detailed ones are heavy.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(shader), SHADERS, 14, 0, SHADER_ORDER},
    {NULL, "AMD FidelityFX Super Resolution",
     "Scales the picture to your screen before the shader, following the edges of the image, and sharpens it (AMD FSR 1).",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(fsr), OFF_ON, 2, 0},
    {NULL, "AMD FidelityFX CAS", "With the shader off: a light, contrast-aware sharpening (AMD FidelityFX CAS) that makes an upscaled picture crisper without halos.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(sharpen), SHARPENS, 3, 0},
    {NULL, "Crop black edges", "Hides the black lines many games leave at the top and bottom, which an old TV hid. Pair with Stretch to fill the screen.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(crop_edges), CROPS, 3, 0},
    {NULL, "Manual crop", "Ignores the crop mode and draws the whole picture, whose area you set yourself. Open the preview with {Touchpad}: {Left}/{Right} stretch or narrow the picture keeping its aspect ratio, and {Up}/{Down} move it up or down. Turn it off and everything is as it was.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(manual_crop), OFF_ON, 2, 0},
    {NULL, "Generate frames",
     "Adds frames AMD FSR 3 makes between the game's own, so motion looks smoother. x2: one between each two, so 30 fps shows at 60 and, with Video output at 120 Hz, 60 fps shows at 120. x4: three between each two, only with Video output at 120 Hz: a 30 fps game shows at 120. With Video output at 60 Hz it only doubles. Each extra frame adds a little input lag. It works at once. Video output is changed above and applies the next time the app starts.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(framegen), FRAMEGENS, 3, 0},
    {NULL, "Load HD textures",
     "Loads HD texture packs. Over FTP, go to /data/Odin/cache/ and create the folder textures if it doesn't exist, then create your game's folder inside it, for example \"SCUS-94900\" (Crash Bandicoot), and put the pack's PNG files there. Needs the GPU renderer. Takes effect with the next game.",
     K_TOGGLE, APPLY_NEXT_GAME, SP_HDTEX, false, NO_FIELD, OFF_ON, 2, 0},
    {"Screen fit", "Integer scaling", "Whole-number scale factors only: even pixels, black borders.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(integer_scale), OFF_ON, 2, 0},
    {NULL, "Smooth final scaling", "Softens the last step up to your TV's resolution.", K_TOGGLE,
     APPLY_NOW, SP_NONE, false, BOOL_FIELD(smooth), OFF_ON, 2, 0},
    {NULL, "Video output",
     "The refresh rate the picture is sent to your TV at: 60 Hz or 120 Hz. 120 Hz needs a TV that supports it; if not, it stays at 60 Hz. It takes effect the next time the app starts.",
     K_CHOICE, APPLY_NEXT_LAUNCH, SP_NONE, true, INT_FIELD(video_out), VIDEO_OUTS, 2, 0},
    {NULL, "Video sync", "On: the game is synchronised to the screen: one frame per refresh, so motion is smoother, and the sound is sped up or slowed down by a hair (under 0.5 %), which may bring a barely noticeable change in pitch. Off: it is synchronised to the sound: the audio is exact, but now and then a frame is repeated or skipped. It only works when the screen's rate and the game's are nearly equal (60 Hz screen and an NTSC game); otherwise the sound sync is used.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(frame_pacing), OFF_ON, 2, 0},
    {"Look", "CRT scanlines", "Dark lines between the picture's lines, like an old TV.", K_CHOICE, APPLY_NOW,
     SP_NONE, false, INT_FIELD(crt), CRT_LEVELS, 3, 0},
    {NULL, "Border", "What surrounds a 4:3 picture: black, the current background, or a TV.", K_CHOICE, APPLY_NOW,
     SP_NONE, false, INT_FIELD(border), BORDERS, 3, 0},
    {NULL, "Game artwork border", "Shows the game's own artwork around a 4:3 picture instead of the border, when there is one (downloaded with the covers).",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(bezel), OFF_ON, 2, 0},
    {"Colour", "Brightness", "Makes the game picture darker or brighter, for dark games or a dim TV.", K_CHOICE,
     APPLY_NOW, SP_NONE, false, INT_FIELD(brightness), BRIGHTNESS, 4, 0},
    {NULL, "Colours", "Vivid: richer colours. Soft: gentler. Warm and Cool shift the tint. Black and white, for fun.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(colour), COLOURS, 6, 0},
#if defined(ARES_UWP)
    /* ARES_UWP: HDR output (notes/CONTRACT.md section 3). The same for every game (the display's), applied at once. */
    {"HDR", "HDR", "Off: the picture is always SDR. Auto: HDR when the display supports it (and the console is set to HDR). On: HDR whenever the console can send it.",
     K_CHOICE, APPLY_NOW, SP_HDR, true, INT_FIELD(hdr_mode), HDR_MODES, 3, 0},
    {NULL, "Peak brightness", "The brightest your display can show, in nits. Set it to your TV's peak (HDR settings of the console or the TV's manual): highlights are mapped up to it and not beyond.",
     K_CHOICE, APPLY_NOW, SP_HDR, true, INT_FIELD(hdr_peak_nits), HDR_PEAKS, 8, 0, NULL, HDR_PEAK_VALUES},
    {NULL, "Paper white", "How bright white is in the menus and in pictures without HDR, in nits. 200 suits most rooms; raise it in a bright room.",
     K_CHOICE, APPLY_NOW, SP_HDR, true, INT_FIELD(hdr_paper_white), HDR_PAPERS, 8, 0, NULL, HDR_PAPER_VALUES},
    {NULL, "Game HDR boost", "Expands the game picture (made for SDR TVs) into HDR: highlights and bright colours rise towards the peak brightness. Off shows it as SDR at paper white.",
     K_CHOICE, APPLY_NOW, SP_HDR, true, INT_FIELD(hdr_game_boost), HDR_BOOSTS, 4, 0},
#endif
    {"Overlay", "Show FPS", "Frames per second in the corner while you play. If the game repeats pictures (a 30 fps game on a 60 fps core), its own rate shows in brackets: 60.0 FPS (game 30.0). While frames are being generated, the number is the frames you actually see: 100 FPS (game 25.0).", K_TOGGLE, APPLY_NOW,
     SP_NONE, false, BOOL_FIELD(show_fps), OFF_ON, 2, 0},
};

static const Row GRAPHICS[] = {
    {"Rendering", "Renderer", "The GPU draws fast, with higher internal resolutions and the shaders. The CPU draws at native resolution only. Takes effect with the next game.",
     K_CHOICE, APPLY_NEXT_GAME, SP_NONE, false, INT_FIELD(renderer), RENDERERS, 2, 0},
    {NULL, "Internal resolution", "Draws 3D at a higher resolution: sharper polygons.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(internal_res), INTERNAL, 16, 1},
    {NULL, "Precise geometry (PGXP)", "Stops polygons wobbling and textures warping.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(pgxp), OFF_ON, 2, 0},
    {NULL, "True colour", "Draws in 32-bit colour, without the dot pattern or colour banding. Pairs well with a high internal resolution.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(true_colour), OFF_ON, 2, 0},
    {"Smoothing", "Supersampling", "At 4x or more: the big picture is averaged down to your TV's pixels, for a clean, stable image without shimmering. With the shader off.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(supersampling), OFF_ON, 2, 0},
};

static const Row CONTROLS[] = {
    {"Controller", "Controller", "DualShock gives games the analog sticks; some old games want the digital pad.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(analog), PADS, 2, 0},
    {NULL, "Left stick as D-pad", "Lets the left stick move in games that only read the D-pad.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(stick_dpad), STICK_MODES, 3, 0},
    {NULL, "Vibration", "Passes the game's rumble to your controller.", K_CHOICE, APPLY_NOW, SP_NONE,
     false, INT_FIELD(vibration), VIBRATIONS, 5, 0},
    {NULL, "Light bar", "The controller's light: as the system sets it, blue/red/green/pink by player, the colour of the game's cover, or red in analog mode and off in digital.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(lightbar), LIGHTBARS, 4, 0},
    {NULL, "Players", "Up to 4 with a multitap, for games that support it: each PS5 controller is a player.",
     K_CHOICE, APPLY_NEXT_GAME, SP_NONE, false, BOOL_FIELD(multitap), PLAYERS, 2, 0},
    {NULL, "Player order", "Choose which controller is player 1, 2, 3 and 4: press {Cross} on each in turn. Kept until Odin closes.",
     K_ACTION, APPLY_NOW, SP_PLAYERS, true, NO_FIELD, NULL, 0, 0},
    {"Sticks", "Dead zone", "Ignores small movements near the centre: raise it if a worn stick drifts on its own.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(stick_deadzone), DEADZONES, 5, 0},
    {NULL, "Stick response", "Precise: small moves stay small, for careful aiming. Quick: a little tilt goes far.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(stick_response), RESPONSES, 3, 0},
    {"DualSense", "Rumble feel", "Classic as the game sends it; Soft is gentler; Punchy makes light hits easier to feel, and can shake the triggers too.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(rumble_feel), RUMBLE_FEELS, 4, 0},
    {NULL, "Trigger effects", "The adaptive triggers. Automatic: {R2} firms up like a gas pedal in racing games (when the NeGcon or the {R2} / {L2} swap is on) and clicks with a light gun. The others give {L2} and {R2} that feel in every game.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(trigger_effects), TRIGGER_FEELS, 6, 0},
    {NULL, "Swap {R2} / {L2} with {Cross} / {Square}", "In every game, {R2} and {Cross} swap places, and so do {L2} and {Square}. Handy in racing games where {Cross} is the gas and {Square} the brake (Gran Turismo, Ridge Racer...): the triggers accelerate and brake.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(racing_triggers), OFF_ON, 2, 0},
    {NULL, "NeGcon in compatible games", "Games that support Namco's NeGcon (racing games, Ace Combat...) use it: how far you press {R2} and {L2} counts, and the left stick steers.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(negcon), OFF_ON, 2, 0},
    {NULL, "Touchpad as a mouse", "Games that support the PS1 Mouse (Discworld, Lemmings...): slide a finger on the {Touchpad} to move the pointer; {R2} or {Cross} clicks, {L2} or {Circle} right-clicks.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(touch_mouse), OFF_ON, 2, 0},
    {NULL, "Light gun", "GunCon and Justifier games: point the controller at the screen to aim (motion sensor), {R2} fires, {L2} reloads, {R3} re-centres. Automatic turns it on in every game that supports a light gun.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(lightgun), LIGHTGUNS, 3, 0},
    {"Buttons", "Touchpad shortcuts", "Hold the {Touchpad} and press: {R2} fast forward, {L2} rewind (turn Rewind on in System), {Square} screenshot, {Triangle} start/pause the timer, {Circle} reset it.",
     K_INFO, APPLY_NOW, SP_HOTKEYS, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Button mapping", "Choose what each button of your controller presses.", K_ACTION,
     APPLY_NOW, SP_REMAP, false, NO_FIELD, NULL, 0, 0},
#if defined(ARES_UWP)
    /* ARES_UWP: on the Xbox all three combinations open the game menu, for every system (main.c game_screen) */
    {"Game menu", "Open the game menu", "During a game, any of these opens the game menu, whatever the system: {L3} + {R3} held for the time below, {L1} + {R1} held for 1 second, or {Touchpad} + {Options} held for half a second.",
     K_INFO, APPLY_NOW, SP_MENU_COMBO, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Game menu hold time", "How long {L3} + {R3} must be held to open the game menu. With Immediate, it opens as soon as both are pressed. {L1} + {R1} always takes 1 second and {Touchpad} + {Options} half a second.",
     K_CHOICE, APPLY_NOW, SP_NONE, true, INT_FIELD(menu_hold), MENU_HOLDS, 3, 0},
#endif
};

static const Row SOUND[] = {
    {"Interface", "Interface sound", "The click when you move through menus and the shelf.", K_CHOICE,
     APPLY_NOW, SP_SOUND, true, INT_FIELD(ui_sound), SOUND_STYLES, 6, 0},
    {NULL, "Interface volume", "How loud the interface sounds are.", K_CHOICE, APPLY_NOW, SP_SOUND,
     true, INT_FIELD(ui_volume), VOLUMES, 4, 0},
};

#if !defined(ODIN_UWP) /* ODIN_UWP: no RetroAchievements tab in the Odin build (the table is not compiled) */
static const Row ACHIEVEMENTS[] = {
    {"Account", "Signed in as", "Use Sign in below with your user name and password. Only a token stays on the PS5.",
     K_INFO, APPLY_NOW, SP_ACCOUNT, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Your profile", "Your points, games mastered, what you unlocked lately and the games closest to mastery.",
     K_ACTION, APPLY_NOW, SP_PROFILE, true, NO_FIELD, NULL, 0, 0},
    {"While playing", "Unlock pop-ups", "The banner when you earn an achievement. Off only hides it: achievements still unlock.",
     K_TOGGLE, APPLY_NOW, SP_NONE, true, BOOL_FIELD(ra_popups), OFF_ON, 2, 0},
    {NULL, "Pop-up style", "Banner: top right. Compact: a small line at the top. Big trophy: in the middle, like the PS5's.",
     K_CHOICE, APPLY_NOW, SP_NONE, true, INT_FIELD(ra_popup_style), POPUP_STYLES, 3, 0},
    {NULL, "Progress tracker", "The small card when a counted achievement moves (18/80 dragons).", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(ra_tracker), OFF_ON, 2, 0},
    {"Play", "Hardcore mode", "Earn hardcore achievements: save states and cheats are off. Turning it on restarts the game.",
     K_TOGGLE, APPLY_NOW, SP_HARDCORE, true, NO_FIELD, OFF_ON, 2, 0},
};
#endif /* !ODIN_UWP */

static const Row LIBRARY[] = {
    {"Shelf", "Theme", "The look of Odin: colours, fonts and the shelf's background.", K_CHOICE, APPLY_NOW, SP_THEME,
     true, INT_FIELD(theme), THEME_NAMES, THEME_COUNT, 0},
    /* the pre-rendered 3D box covers are no longer used: the covers are always flat
    {NULL, "Cover art", "Flat front covers, or 3D boxes.", K_CHOICE, APPLY_NOW, SP_NONE, true,
     INT_FIELD(cover_style), COVER_STYLES, 2, 0},
    */
    {NULL, "Jewel cases", "The shelf shows 3D jewel cases. Off: the flat covers with a reflection on the floor, as in version 1.1.8.", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(jewel_cases), OFF_ON, 2, 0},
    {NULL, "Cover lighting", "The light on the cover in the jewel case: its shading and the glare over the lid. Off: the picture as it is.", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(case_lighting), OFF_ON, 2, 0},
    {NULL, "Disc animation", "When a game starts, its disc slides out of the case and spins.", K_TOGGLE, APPLY_NOW,
     SP_NONE, true, BOOL_FIELD(disc_animation), OFF_ON, 2, 0},
    {NULL, "Disc in tray", "The disc stays on its tray while it spins: it does not lift or grow, as in version 1.3.7.", K_TOGGLE, APPLY_NOW,
     SP_NONE, true, BOOL_FIELD(disc_in_tray), OFF_ON, 2, 0},
    {NULL, "Download missing covers", "Fetches covers for new games over the internet.", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(cover_download), OFF_ON, 2, 0},
    {NULL, "Background", "Classic theme: Dark keeps the screen deep navy; Cover colour tints it with the selected game.",
     K_CHOICE, APPLY_NOW, SP_NONE, true, INT_FIELD(background), BACKGROUNDS, 2, 0},
    {NULL, "Sort by", "The order of games on the shelf ({Options} on the shelf changes it too).", K_CHOICE,
     APPLY_NOW, SP_NONE, true, INT_FIELD(sort_mode), SORTS, 4, 0},
    {"Games", "Your library", "How much you've played: games, hours, achievements and your most played.", K_ACTION,
     APPLY_NOW, SP_STATS, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Memory cards", "Every game's memory card: see the saves, export a card, import one.", K_ACTION,
     APPLY_NOW, SP_MEMCARDS, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Rescan library", "Looks for games added to /data/Odin/games or a USB drive.", K_ACTION,
     APPLY_NOW, SP_RESCAN, true, NO_FIELD, NULL, 0, 0},
};

static const Row SYSTEM[] = {
#if !defined(ODIN_UWP) /* ODIN_UWP: no PS1 BIOS start in the Odin build */
    {"Odin", "Start BIOS", "Starts the console's BIOS with no disc, to manage memory cards. While Memory Card 1 Type (Restart) is set to Libretro, the saves will not show: that card belongs to each game, and with no game there is none.",
     K_ACTION, APPLY_NOW, SP_BIOS_START, true, NO_FIELD, NULL, 0, 0},
#endif
    {NULL, "Ask when a disc is put in", "A disc put in the USB CD drive opens its question by itself: play it from the disc or copy it, or start its game if it is installed already. Off: its game is only shown on the shelf, and the question comes when {Cross} is pressed on it.", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(disc_ask), OFF_ON, 2, 0},
    {NULL, "Install to external memory", "A disc is installed on the extended storage when it has at least 1 GB free, in its Odin/games folder, instead of /data. Off: it is installed in /data.", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(install_to_ext), OFF_ON, 2, 0},
#if defined(SWANUI_XBOX) /* SWANUI_XBOX: the Xbox folder picker instead of a typed folder name */
    {NULL, "Games folder", "Choose the folder with your games with the Xbox file picker. Games can be directly in it or one folder per game.",
     K_ACTION, APPLY_NOW, SP_USBFOLDER, true, NO_FIELD, NULL, 0, 0},
#else
    {NULL, "Games folder on drives", "A folder name to look for games in on drives. It is looked for on /mnt/ext0/, /mnt/ext1/ and /mnt/usb0/ to /mnt/usb7/ at once, as the number changes with the drives plugged in: psx searches /mnt/usb0/psx, /mnt/usb1/psx and so on. Leave it empty to turn it off.",
     K_ACTION, APPLY_NOW, SP_USBFOLDER, true, NO_FIELD, NULL, 0, 0},
#endif
    {NULL, "NFS shares", "Games on an NFS server of your network, as nfs:// addresses separated by ';'. Example: nfs://192.168.1.85/psx. The shelf lists them like a drive's; only Odin sees them. The server has to export the folder to the console. Rescan library connects the ones that did not.",
     K_ACTION, APPLY_NOW, SP_NFSHARES, true, NO_FIELD, NULL, 0, 0},
    {NULL, "NTFS disks", "Games on a USB disk formatted as NTFS, read straight from the disk and never copied. The shelf lists the Games folder on drives (and Odin/games) of each disk; only Odin sees them. Rescan library looks for disks plugged in after the start. A disk Windows left in Fast Startup may not show its latest changes.", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(ntfs_enabled), OFF_ON, 2, 0},
    {NULL, "NFS timeout", "How long a read from an NFS server may take before the connection is given up and the game is left. While it waits, the game stays paused and tries to reconnect. Raise it if the server or the network is slow.",
     K_CHOICE, APPLY_NOW, SP_NONE, true, INT_FIELD(nfs_timeout), NFS_TIMEOUTS, 5, 0},
#if !defined(ODIN_UWP) /* ODIN_UWP: PS1 options are not in the Odin build */
    {"Emulation", "Region", "Auto follows the disc; force 50 or 60 Hz, or the Japanese console, if a game misbehaves.", K_CHOICE,
     APPLY_NEXT_GAME, SP_NONE, false, INT_FIELD(region), REGIONS, 3, 0},
#endif
#if !defined(ODIN_UWP) /* ODIN_UWP: PS1 options are not in the Odin build */
    {NULL, "Fast CD loading", "Shorter loading screens; the higher, the faster. Rarely, a game glitches.", K_CHOICE, APPLY_NOW,
     SP_NONE, false, INT_FIELD(cd_speed), CD_SPEEDS, 10, 1},
#endif
#if !defined(ODIN_UWP) /* ODIN_UWP: PS1 options are not in the Odin build */
    {NULL, "PS1 startup intro", "Shows the PS1 logo and sound before the game, as a real console does. Needs your own BIOS.",
     K_TOGGLE, APPLY_NEXT_GAME, SP_NONE, false, BOOL_FIELD(boot_intro), OFF_ON, 2, 0},
#endif
    {NULL, "Known game fixes", "For that game only, applies what the game database knows it needs: turns off the settings it breaks with (PGXP, widescreen, upscaling, filtering) and turns on what it requires (CPU cache, PGXP options, screen and DMA timing).",
     K_TOGGLE, APPLY_NEXT_GAME, SP_NONE, false, BOOL_FIELD(game_fixes), OFF_ON, 2, 0},
    {"Playing", "Quick resume", "Saves when you leave a game, so the shelf can offer Continue.", K_TOGGLE,
     APPLY_NOW, SP_NONE, true, BOOL_FIELD(quick_resume), OFF_ON, 2, 0},
    {NULL, "Rewind", "Keeps the last 8 seconds so you can go back ({Touchpad} + {L2}). Uses about 200 MB of memory.",
     K_TOGGLE, APPLY_NOW, SP_NONE, false, BOOL_FIELD(rewind), OFF_ON, 2, 0},
#if !defined(ODIN_UWP) /* ODIN_UWP: PS1 options are not in the Odin build */
    {NULL, "Run-ahead", "Answers your buttons 1 to 10 frames sooner, for fighting games and platformers. It runs extra frames: at high internal resolutions some games may slow down. It is not used while Double frames is on.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(run_ahead), RUN_AHEADS, 11, 0},
#endif
    {NULL, "Auto-save", "Saves the game by itself every few minutes into three auto-save slots (the oldest is replaced). Load one from the Odin menu > Auto-saves.",
     K_CHOICE, APPLY_NOW, SP_NONE, false, INT_FIELD(autosave), AUTOSAVES, 4, 0},
    {"People", "Who's playing",
#if defined(ODIN_UWP)
"Profiles: each person keeps their own settings, play time and favourites. Games and covers are shared.",
#else
"Profiles: each person keeps their own memory cards, save states, settings, play time, favourites and RetroAchievements sign-in. Games and covers are shared.",
#endif
     K_ACTION, APPLY_NOW, SP_WHO, true, NO_FIELD, NULL, 0, 0},
#if !defined(ODIN_UWP) /* ODIN_UWP: no phone remote in the Odin build */
    {"Phone", "Settings from your phone", "Change settings from a phone on the same network: scan the code.",
     K_TOGGLE, APPLY_NOW, SP_REMOTE, true, BOOL_FIELD(remote), OFF_ON, 2, 0},
#endif
    {"Console", "Language", "The language of Odin's menus.", K_CHOICE, APPLY_NOW, SP_LANGUAGE, true,
     INT_FIELD(language), NULL, LANG_COUNT, 0},
#if !defined(ODIN_UWP) /* ODIN_UWP: PS5 hardware services are not in the Odin build */
    {NULL, "Unlock /data with etaHEN", "Odin asks etaHEN for access to your games. Turn off if closing Odin crashes the console.",
     K_TOGGLE, APPLY_NEXT_LAUNCH, SP_UNLOCK, true, NO_FIELD, OFF_ON, 2, 0},
#endif
#if !defined(ODIN_UWP) /* ODIN_UWP: PS5 hardware services are not in the Odin build */
    {NULL, "Allow Odin in PS5SX2 Helper", "PS5SX2 Helper only unlocks the apps listed in /data/whitelist.txt. This adds Odin; reload the helper (or restart the console) afterwards.",
     K_ACTION, APPLY_NOW, SP_WHITELIST, true, NO_FIELD, NULL, 0, 0},
#endif
};

#if defined(SWANUI_XBOX)
/* SWANUI_XBOX (S5): what this build is, where its files live, and everyone it is built on. Every row can be selected
 * (selectable()) so each help can be read. No row may be hidden: xbox_filter_tabs() skips this table. */
static const char *const XB_THIS_BUILD[] = {"Private Xbox port"};
#if defined(TSUGARU_UWP)
static const char *const XB_INTERFACE[] = {"Odin " SwanStationPS5_VERSION "Odin: see the credits file in the package"}; /* TSUGARU_UWP: credited as is (swanui_xbox_brand skips it) */
#else
static const char *const XB_INTERFACE[] = {"Odin " SwanStationPS5_VERSION};
#endif
static const char *const XB_ORIGIN[] = {"PSXS5 by SynoPiia"};
static const char *const XB_LICENCES[] = {"GPL-3.0+ and each core's own licence"};
static const char *const XB_THANKS[] = {"Covers, fonts, icons, cheats..."};
#if defined(ODIN_UWP) /* ODIN_UWP: the Odin shell's About (no PS1, no Tsugaru, no FM Towns) */
static const Row ABOUT[] = {
    {"Odin (experimental)", "Version", "Odin 0.2.0.0: the Odin shell with odin_core.dll (ABI 1). No picture yet, so no game runs in this build.",
     K_INFO, APPLY_NOW, SP_VERSION, true, NO_FIELD, NULL, 0, 0},
    {NULL, "This build", "An unofficial build for personal use on a console in Developer Mode. It is not published and must not be shared.",
     K_INFO, APPLY_NOW, SP_NONE, true, NO_FIELD, XB_THIS_BUILD, 1, 0},
    {NULL, "BIOS", "Never included: the shared sets (hikaru.zip, mie.zip) are yours to copy into the bios folder of the app's data.",
     K_INFO, APPLY_NOW, SP_BIOS, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Games", "Your own sets: in the games folder you choose with the Xbox file picker, or in the app's games folder.",
     K_INFO, APPLY_NOW, SP_GAMES, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Drives", "The USB drives the app can read, with their free space.",
     K_INFO, APPLY_NOW, SP_DRIVES, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Data folder", "Saves, states and settings live here, in the app's LocalState (Device Portal > File explorer). Uninstalling the app deletes it.",
     K_INFO, APPLY_NOW, SP_DATA, true, NO_FIELD, NULL, 0, 0},
    {"Credits", "Licences", "The notices of this build and the GPL-3.0 text are in the credits folder of the package.",
     K_INFO, APPLY_NOW, SP_NONE, true, NO_FIELD, NULL, 0, 0},
};
#else
static const Row ABOUT[] = {
    {"Odin", "Version", /* TSUGARU_UWP branding (ARES_UWP: swanui_xbox_brand shows "ares") */
     "Odin: the Odin interface brought to the Xbox, running on the emulation core named under Credits.",
     K_INFO, APPLY_NOW, SP_VERSION, true, NO_FIELD, NULL, 0, 0},
    {NULL, "This build",
     "An unofficial build for personal use on a console in Developer Mode. It is not published and must not be shared: the licences of its parts do not allow them to be distributed together (see Licences).",
     K_INFO, APPLY_NOW, SP_NONE, true, NO_FIELD, XB_THIS_BUILD, 1, 0},
    {NULL, "BIOS", "Never included: copy your own console's BIOS (SCPH1001.BIN is recommended) into Odin\\bios with Device Portal.",
     K_INFO, APPLY_NOW, SP_BIOS, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Games", "Your own backups: in Odin\\games, or on a USB drive in Odin, Odin\\games or the Games folder on drives.",
     K_INFO, APPLY_NOW, SP_GAMES, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Drives", "The USB drives the app can read, with their free space. A drive must be NTFS, readable by ALL APPLICATION PACKAGES, and set up as Media on the console.",
     K_INFO, APPLY_NOW, SP_DRIVES, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Data folder", "Saves, states, covers and settings live here, in the app's LocalState (Device Portal > File explorer). Uninstalling the app deletes it.",
     K_INFO, APPLY_NOW, SP_DATA, true, NO_FIELD, NULL, 0, 0},
    {"Credits", "Created by", "The person behind Odin.", K_INFO, APPLY_NOW, SP_CREDITS, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Interface",
     "Odin: see the credits file in the package",
     K_INFO, APPLY_NOW, SP_NONE, true, NO_FIELD, XB_INTERFACE, 1, 0},
    {NULL, "Began as", "Odin: see the credits file in the package",
     K_INFO, APPLY_NOW, SP_NONE, true, NO_FIELD, XB_ORIGIN, 1, 0},
    {NULL, "Emulation core",
     "Odin: see the credits file in the package",
     K_INFO, APPLY_NOW, SP_CORE, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Licences",
     "Odin: see the credits file in the package",
     K_INFO, APPLY_NOW, SP_NONE, true, NO_FIELD, XB_LICENCES, 1, 0},
    {NULL, "Thanks",
     "RetroAchievements and rcheevos; xlenore/psx-covers and libretro-thumbnails (covers, art); the libretro-database contributors (cheats); sodaraptor's Hypnagogia Boundless Dreams Jewel Case (CC BY 4.0, adapted); Inter, Noto Sans JP and the theme fonts (SIL OFL 1.1); Tabler Icons, stb and Nayuki's QR Code generator. Full list: Odin's THIRD_PARTY_NOTICES.md.",
     K_INFO, APPLY_NOW, SP_NONE, true, NO_FIELD, XB_THANKS, 1, 0},
};
#endif /* ODIN_UWP */
#else
static const Row ABOUT[] = {
    {"Odin", "Version", "PS1 X Super 5: PS1 emulation for jailbroken PS5, built on Odin.",
     K_INFO, APPLY_NOW, SP_VERSION, true, NO_FIELD, NULL, 0, 0},
    {NULL, "BIOS", "Odin never includes a BIOS: use a dump of your own console, or the built-in one.",
     K_INFO, APPLY_NOW, SP_BIOS, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Games", "Games are your own backups, in /data/Odin/games.", K_INFO, APPLY_NOW, SP_GAMES,
     true, NO_FIELD, NULL, 0, 0},
    {NULL, "Drives", "The extended storage and USB drives found, with their free space.", K_INFO, APPLY_NOW,
     SP_DRIVES, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Data folder", "Saves, states, covers and settings live here.", K_INFO, APPLY_NOW, SP_DATA,
     true, NO_FIELD, NULL, 0, 0},
    {NULL, "Created by", "The person behind Odin.", K_INFO, APPLY_NOW, SP_CREDITS, true, NO_FIELD, NULL, 0,
     0},
    {"Updates", "Check for updates", "Looks for a newer Odin on GitHub and installs it.", K_ACTION, APPLY_NOW,
     SP_UPDATE, true, NO_FIELD, NULL, 0, 0},
    {NULL, "Check when Odin starts", "Tells you on the shelf when a new version is out.", K_TOGGLE, APPLY_NOW,
     SP_NONE, true, BOOL_FIELD(update_check), OFF_ON, 2, 0},
};
#endif

typedef struct
{
    const char *name;
    int icon;
    const Row *rows;
    int count;
} Tab;

#define TAB(n, i, r) {n, i, r, (int)(sizeof(r) / sizeof(r[0]))}
#if defined(ARES_UWP)
/* ARES_UWP: the Systems tab, one row per system of the registry (built by build_system_rows): this placeholder is
 * replaced the first time a tab is used */
static const Row SYSTEMS[] = {
    {"Systems", "Systems", "", K_INFO, APPLY_NOW, SP_NONE, true, NO_FIELD, NULL, 0, 0},
};
#endif
static Tab TABS[] = {
#if defined(ARES_UWP)
    TAB("Systems", ICON_CATEGORY, SYSTEMS), /* ARES_UWP: each system's own options, first */
#endif
    TAB("Display", ICON_DEVICE_TV, DISPLAY),
    TAB("Graphics", ICON_SPARKLES, GRAPHICS),
    TAB("Controls", ICON_DEVICE_GAMEPAD_2, CONTROLS),
    TAB("Sound", ICON_VOLUME, SOUND),
#if !defined(ODIN_UWP)
    TAB("Achievements", ICON_TROPHY, ACHIEVEMENTS),
#endif
    TAB("Library", ICON_BOOKS, LIBRARY),
#if defined(ARES_UWP)
    TAB("General", ICON_CPU, SYSTEM), /* ARES_UWP: "System" beside "Systems" read as the same thing */
#else
    TAB("System", ICON_CPU, SYSTEM),
#endif
    TAB("About", ICON_INFO_CIRCLE, ABOUT),
};
#define TAB_MAX (int)(sizeof(TABS) / sizeof(TABS[0]))
#if defined(ARES_UWP)
static int tab_count = TAB_MAX; /* ARES_UWP: a tab left with no row (Graphics: every row is the PS1's) is taken out */
#define TAB_COUNT tab_count
#else
#define TAB_COUNT TAB_MAX
#endif

/* SwanStation's own core options (swanstation_options.h, made by tools/gen_swanstation_options.py), added to
 * the tab each belongs to: console and advanced to System, enhancements to Graphics, display to Display, and
 * the controller ports to Controls. Built once, the first time a tab is used. */
#define MAX_ROWS 160
static bool ss_rows_built;

static bool is_swanstation_row(const Row *r)
{
    return r->offset >= (int)offsetof(Settings, ss_opt) &&
           r->offset < (int)(offsetof(Settings, ss_opt) + sizeof(((Settings *)0)->ss_opt));
}

/* The core reads these only when a game boots or loads (BIOS, disc loading, RAM size, memory card 1, the audio hook
 * and the software readback renderer); every other option of its own takes effect while the game runs. */
static bool ss_needs_reload(const char *key)
{
    static const char *const reload[] = {
        "odin_BIOS_PathNTSCJ", "odin_BIOS_PathNTSCU", "odin_BIOS_PathPAL",
        "odin_CDROM_ReadThread", "odin_CDROM_LoadImagePatches", "odin_CDROM_LoadImageToRAM",
        "odin_CDROM_PreCacheCHD", "odin_GPU_UseSoftwareRendererForReadbacks",
        "odin_MemoryCards_Card1Type", "odin_Console_Enable8MBRAM", "odin_Audio_FastHook"};
    for (size_t i = 0; i < sizeof(reload) / sizeof(reload[0]); ++i)
        if (strcmp(key, reload[i]) == 0)
            return true;
    return false;
}

/* The core's aspect ratio is the only one: it goes first in the Picture group, as "Aspect ratio", with the
 * widescreen hack under it, not with the rest of the core's display and enhancement options. */
static bool is_aspect_option(int i)
{
    return strcmp(SS_OPTS[i].key, "odin_Display_AspectRatio") == 0;
}

static bool is_custom_aspect_option(int i)
{
    return strcmp(SS_OPTS[i].key, "odin_Display_CustomAspectRatioNumerator") == 0 ||
           strcmp(SS_OPTS[i].key, "odin_Display_CustomAspectRatioDenominator") == 0;
}

static bool is_widescreen_option(int i)
{
    return strcmp(SS_OPTS[i].key, "odin_GPU_WidescreenHack") == 0;
}

static bool is_texture_filter_option(int i)
{
    return strcmp(SS_OPTS[i].key, "odin_GPU_TextureFilter") == 0;
}

static bool is_sprite_filter_option(int i)
{
    return strcmp(SS_OPTS[i].key, "odin_GPU_SpriteTextureFilter") == 0;
}

/* Turned on only by the game database, for the games that need them */
static bool is_db_only_option(int i)
{
    return strncmp(SS_OPTS[i].key, "odin_GPU_FilterFramebufferUploads", 40) == 0 ||
           strcmp(SS_OPTS[i].key, "odin_GPU_ForceRoundTextureCoordinates") == 0 ||
           strcmp(SS_OPTS[i].key, "odin_GPU_LineDetectMode") == 0 ||
           strcmp(SS_OPTS[i].key, "odin_GPU_TrueColorFull") == 0 ||
           strcmp(SS_OPTS[i].key, "odin_GPU_PGXPNormalizeDepth") == 0;
}

static int ac_opt(void);

static void add_swanstation_rows(Tab *tab, const Row *base, int base_count, const int *categories, int ncat)
{
    static char groups[SSC_COUNT][48];
    int extra = 0, aspect = -1, wide = -1, tfilter = -1, sfilter = -1, custom[2] = {-1, -1};
    for (int i = 0; i < SS_OPT_COUNT; ++i)
    {
        if (base == DISPLAY && is_aspect_option(i))
            aspect = i;
        if (base == DISPLAY && is_widescreen_option(i))
            wide = i;
        if (base == DISPLAY && is_custom_aspect_option(i))
            custom[custom[0] < 0 ? 0 : 1] = i;
        if (base == DISPLAY && is_texture_filter_option(i))
            tfilter = i;
        if (base == DISPLAY && is_sprite_filter_option(i))
            sfilter = i;
    }
    for (int c = 0; c < ncat; ++c)
        for (int i = 0; i < SS_OPT_COUNT; ++i)
            extra += SS_OPTS[i].category == categories[c];
    Row *rows = malloc(sizeof(Row) * (size_t)(base_count + extra + 6));
    if (!rows)
        return;
    int n = 0;
    if (aspect >= 0)
    {
        const Row nostalgia = {"Picture",
                               "Nostalgia mode",
                               "While a game is played, the app makes no sounds and no notices, trophies or progress cards appear. The game menu still opens with its combination (Settings > Controls).",
                               K_TOGGLE,
                               APPLY_NOW,
                               SP_NONE,
                               true,
                               BOOL_FIELD(nostalgia),
                               OFF_ON,
                               2,
                               0};
        rows[n++] = nostalgia;
        Row r = {NULL,
                 "Aspect ratio",
                 "The shape of the picture. Pair 16:9 with a widescreen cheat. With Custom, set the ratio with the numerator and denominator under Screen fit.",
                 K_CHOICE,
                 APPLY_NOW,
                 SP_NONE,
                 false,
                 (int)(offsetof(Settings, ss_opt) + (size_t)aspect * sizeof(int)),
                 false,
                 SS_OPTS[aspect].labels,
                 SS_OPTS[aspect].count,
                 0};
        rows[n++] = r;
        if (wide >= 0)
        {
            r.group = NULL;
            r.name = SS_OPTS[wide].label;
            r.help = "Increases the field of view from 4:3 to the chosen aspect ratio in 3D games. It only has an effect with an aspect ratio wider than 4:3, such as 16:9. For 2D games, or games which use pre-rendered backgrounds, this enhancement will not work as expected.";
            r.offset = (int)(offsetof(Settings, ss_opt) + (size_t)wide * sizeof(int));
            r.labels = SS_OPTS[wide].labels;
            r.count = SS_OPTS[wide].count;
            rows[n++] = r;
        }
        if (tfilter >= 0)
        {
            r.group = NULL;
            r.name = SS_OPTS[tfilter].label;
            r.help = SS_OPTS[tfilter].help;
            r.offset = (int)(offsetof(Settings, ss_opt) + (size_t)tfilter * sizeof(int));
            r.apply = ss_needs_reload(SS_OPTS[tfilter].key) ? APPLY_NEXT_GAME : APPLY_NOW;
            r.labels = ss_opt_labels(tfilter);
            r.count = ss_opt_count(tfilter);
            rows[n++] = r;
        }
        if (sfilter >= 0)
        {
            r.group = NULL;
            r.name = SS_OPTS[sfilter].label;
            r.help = SS_OPTS[sfilter].help;
            r.offset = (int)(offsetof(Settings, ss_opt) + (size_t)sfilter * sizeof(int));
            r.apply = ss_needs_reload(SS_OPTS[sfilter].key) ? APPLY_NEXT_GAME : APPLY_NOW;
            r.labels = ss_opt_labels(sfilter);
            r.count = ss_opt_count(sfilter);
            rows[n++] = r;
        }
    }
    memcpy(rows + n, base, sizeof(Row) * (size_t)base_count);
    if (aspect >= 0)
        rows[n].group = NULL; /* "Picture" is now on the aspect row */
    n += base_count;
    if (aspect >= 0)
    {
        /* Manual crop goes right under "Aspect ratio" */
        for (int k = 3; k < n; ++k)
        {
            if (strcmp(rows[k].name, "Manual crop") != 0)
                continue;
            const Row m = rows[k];
            memmove(rows + 3, rows + 2, sizeof(Row) * (size_t)(k - 2));
            rows[2] = m;
            break;
        }
    }
    /* the three BIOS choices and the BIOS to start go above "Start BIOS", at the top of the SwanStationPS5 group */
    if (base == SYSTEM)
    {
        for (int k = 0; k < n; ++k)
        {
            if (strcmp(rows[k].name, "Start BIOS") != 0)
                continue;
            memmove(rows + k + 4, rows + k, sizeof(Row) * (size_t)(n - k));
            rows[k + 4].group = NULL;
            int placed = 0;
            for (int i = 0; i < SS_OPT_COUNT && placed < 3; ++i)
            {
                if (!ss_opt_is_bios(i))
                    continue;
                static const char *const bios_names[] = {"NTSC-J BIOS", "NTSC-U BIOS", "PAL BIOS"}; /* the table's order */
                Row r = {placed == 0 ? "Odin" : NULL,
                         bios_names[placed],
                         "Which BIOS the games of this region run: detect it automatically, the OpenBIOS built into the core, or a file of the BIOS folder. It applies when the next game starts.",
                         K_CHOICE,
                         APPLY_NEXT_GAME,
                         SP_NONE,
                         true, /* global_only: the console's BIOS is the same for every game */
                         (int)(offsetof(Settings, ss_opt) + (size_t)i * sizeof(int)),
                         false,
                         ss_opt_labels(i),
                         ss_opt_count(i),
                         0};
                rows[k + placed++] = r;
            }
            Row start = {NULL,
                         "BIOS to start",
                         "The console whose BIOS Start BIOS boots: it runs the BIOS chosen above for that region. NTSC-U unless you change it here; it goes back to NTSC-U every time Odin opens.",
                         K_CHOICE,
                         APPLY_NOW,
                         SP_NONE,
                         true,
                         INT_FIELD(bios_start),
                         BIOS_START_REGIONS,
                         3,
                         0};
            rows[k + 3] = start;
            n += 4;
            break;
        }
    }
    /* the custom aspect ratio goes above "Integer scaling", opening the "Screen fit" group */
    if (custom[0] >= 0 && custom[1] >= 0)
    {
        for (int k = 0; k < n; ++k)
        {
            if (strcmp(rows[k].name, "Integer scaling") != 0)
                continue;
            memmove(rows + k + 2, rows + k, sizeof(Row) * (size_t)(n - k));
            rows[k + 2].group = NULL;
            for (int j = 0; j < 2; ++j)
            {
                int i = custom[j];
                Row r = {j == 0 ? "Screen fit" : NULL,
                         SS_OPTS[i].label,
                         SS_OPTS[i].help,
                         K_CHOICE,
                         APPLY_NOW,
                         SP_NONE,
                         false,
                         (int)(offsetof(Settings, ss_opt) + (size_t)i * sizeof(int)),
                         false,
                         SS_OPTS[i].labels,
                         SS_OPTS[i].count,
                         0};
                rows[k + j] = r;
            }
            n += 2;
            break;
        }
    }
    for (int c = 0; c < ncat; ++c)
    {
        bool first = true;
        snprintf(groups[categories[c]], sizeof(groups[0]), "%s (Core)", SS_CATEGORY_NAMES[categories[c]]);
        for (int i = 0; i < SS_OPT_COUNT; ++i)
        {
            if (SS_OPTS[i].category != categories[c] || n >= MAX_ROWS || is_aspect_option(i) || is_widescreen_option(i) ||
                is_texture_filter_option(i) || is_sprite_filter_option(i) || is_db_only_option(i) || ss_opt_is_bios(i) || /* they go with Start BIOS, in SwanStationPS5 */
                (base == DISPLAY && custom[0] >= 0 && is_custom_aspect_option(i)))
                continue;
#if !defined(ARES_UWP) /* ARES_UWP: Controls > Game menu has its own rows (all three combinations) */
            if (base == CONTROLS && i == ac_opt())
            {
                Row m = {first ? groups[categories[c]] : NULL,
                         "Game menu combination",
                         "The buttons that open the game menu during a game: {Touchpad} + {R3}, {L3} + {R3} or {Options} + {Touchpad}. How long they must be held is set in Game menu hold time. A combination the analog mode combo already uses can't be chosen.",
                         K_CHOICE,
                         APPLY_NOW,
                         SP_NONE,
                         true,
                         INT_FIELD(menu_combo),
                         MENU_COMBOS,
                         3,
                         0};
                rows[n++] = m;
                Row h = {NULL,
                         "Game menu hold time",
                         "How long the game menu combination must be held to open the menu. With Immediate, it opens as soon as the buttons are pressed.",
                         K_CHOICE,
                         APPLY_NOW,
                         SP_NONE,
                         true,
                         INT_FIELD(menu_hold),
                         MENU_HOLDS,
                         3,
                         0};
                rows[n++] = h;
                first = false;
            }
#endif
            Row r = {first ? groups[categories[c]] : NULL,
                     SS_OPTS[i].label,
                     SS_OPTS[i].help,
                     K_CHOICE,
                     ss_needs_reload(SS_OPTS[i].key) ? APPLY_NEXT_GAME : APPLY_NOW,
                     SP_NONE,
                     ss_opt_is_bios(i), /* global_only: the console's BIOS is the same for every game */
                     (int)(offsetof(Settings, ss_opt) + (size_t)i * sizeof(int)),
                     false,
                     i == ac_opt() ? ANALOG_COMBOS : ss_opt_labels(i),
                     ss_opt_count(i),
                     0};
            rows[n++] = r;
            first = false;
        }
    }
    tab->rows = rows;
    tab->count = n;
}

#if defined(SWANUI_XBOX)
/* SWANUI_XBOX (S5): rows that do nothing on Xbox (PS5 hardware and services, SwanStation core options DuckStation does
 * not have) are left out of their tab, and a few rows offer fewer choices. port-notes/specs/S5-xbox-ui.md section 4. */
static const char *const XBOX_SHADERS[] = {"Off", "NTSC", "CRT Lottes", "CRT Newpixie", "CRT Hyllian"};
static const unsigned char XBOX_SHADER_ORDER[] = {0, 5, 9, 14, 15}; /* the Settings.shader values S3 maps to a shader */

#if defined(TSUGARU_UWP) || defined(ARES_UWP)
/* TSUGARU_UWP (FM Towns): rows about the PS1 (the SwanStation core options, GPU renderer and PGXP, the DualShock type
 * and multitap, memory cards, the BIOS, region, CD speed, fan-fix database) mean nothing to Tsugaru: hidden, not
 * deleted. Rows are recognised by their English name, as the rest of this file does. */
static bool towns_hidden(const Row *r)
{
    if (is_swanstation_row(r))
        return true;
    if (r->special == SP_BIOS_START || r->special == SP_MEMCARDS)
        return true;
    static const char *const NAMES[] = {"Renderer",
                                        "Internal resolution",
                                        "Precise geometry (PGXP)",
                                        "True colour",
                                        "Supersampling",
                                        "Load HD textures",
                                        "Controller",
                                        "Players",
                                        "NeGcon in compatible games",
                                        "Light gun",
                                        "PS1 startup intro",
                                        "Known game fixes",
                                        "Fast CD loading",
                                        "Region",
                                        "Start BIOS",
                                        "Memory cards"};
    for (size_t k = 0; k < sizeof(NAMES) / sizeof(NAMES[0]); ++k)
        if (r->name && strcmp(r->name, NAMES[k]) == 0)
            return true;
#if defined(ARES_UWP)
    /* ARES_UWP: the same PS1 rows mean nothing to ares either (SwanStation's core options, the PS1 renderer, PGXP,
     * internal resolution, the memory cards and the PS1 BIOS choice, multitap, NeGcon, light gun); and the PlayStation
     * Mouse row */
    if (r->name && strcmp(r->name, "Touchpad as a mouse") == 0)
        return true;
    /* ARES_UWP: SwanStation's run-ahead and video sync (frame pacing): ares paces itself on its own thread; the PS1
     * BIOS region Start BIOS boots (its row has a field of its own, not a core option) */
    if (r->name && (strcmp(r->name, "Run-ahead") == 0 || strcmp(r->name, "Video sync") == 0 ||
                    strcmp(r->name, "BIOS to start") == 0))
        return true;
#endif
    return false;
}
#endif

static bool xbox_hidden(const Row *r)
{
#if defined(TSUGARU_UWP) || defined(ARES_UWP)
    if (towns_hidden(r))
        return true;
#endif
    if (is_swanstation_row(r))
    {
        static const char *const KEYS[] = {
            "odin_CPU_ExecutionMode",          "odin_GPU_UseThread",
            "odin_GPU_ShaderPrecompile",       "odin_Controller_ShowCrosshair",
            "odin_CPU_RecompilerBlockLinking", "odin_CPU_FastmemRewrite",
            "odin_Audio_FastHook"};
        const int i = (r->offset - (int)offsetof(Settings, ss_opt)) / (int)sizeof(int);
        for (size_t k = 0; k < sizeof(KEYS) / sizeof(KEYS[0]); ++k)
            if (strcmp(SS_OPTS[i].key, KEYS[k]) == 0)
                return true;
        return false;
    }
    switch (r->special)
    {
    case SP_UNLOCK:
    case SP_WHITELIST:
    case SP_REMOTE:
    case SP_UPDATE:
    case SP_NFSHARES:
        return true;
    default:
        break;
    }
    static const size_t FIELDS[] = {
        offsetof(Settings, fsr),        offsetof(Settings, sharpen),         offsetof(Settings, framegen),
        offsetof(Settings, video_out),  offsetof(Settings, brightness),      offsetof(Settings, colour),
        offsetof(Settings, lightbar),   offsetof(Settings, trigger_effects), offsetof(Settings, disc_ask),
        offsetof(Settings, install_to_ext), offsetof(Settings, ntfs_enabled), offsetof(Settings, nfs_timeout),
        offsetof(Settings, update_check)};
    for (size_t k = 0; k < sizeof(FIELDS) / sizeof(FIELDS[0]); ++k)
        if (r->offset == (int)FIELDS[k])
            return true;
    return false;
}

static void xbox_fix_row(Row *r)
{
    if (r->offset == (int)offsetof(Settings, shader))
    {
        r->labels = XBOX_SHADERS;
        r->order = XBOX_SHADER_ORDER;
        r->count = (int)(sizeof(XBOX_SHADERS) / sizeof(XBOX_SHADERS[0]));
    }
    else if (r->offset == (int)offsetof(Settings, rumble_feel))
        r->count = 3; /* "Punchy, in the triggers too" needs the DualSense's trigger motors */
}

/* Every tab but About gets a copy without the hidden rows. A hidden row's group caption goes to the next row shown
 * when that one has none, so a group whose first row is hidden keeps its card; a caption whose rows are all hidden
 * disappears. The arrays it replaces (static, or add_swanstation_rows') are built once and not freed. */
static void xbox_filter_tabs(void)
{
    for (int t = 0; t < TAB_COUNT; ++t)
    {
        if (TABS[t].rows == ABOUT)
            continue; /* draw_help() and selectable() recognise About by this pointer; the Xbox About hides nothing */
        Row *rows = malloc(sizeof(Row) * (size_t)(TABS[t].count > 0 ? TABS[t].count : 1));
        if (!rows)
            continue; /* out of memory: the tab keeps every row */
        const char *caption = NULL;
        int n = 0;
        for (int k = 0; k < TABS[t].count; ++k)
        {
            Row r = TABS[t].rows[k];
            if (xbox_hidden(&r))
            {
                if (r.group && !caption)
                    caption = r.group;
                continue;
            }
            if (!r.group)
                r.group = caption;
            caption = NULL;
            xbox_fix_row(&r);
            rows[n++] = r;
        }
        TABS[t].rows = rows;
        TABS[t].count = n;
    }
#if defined(ARES_UWP)
    int kept = 0;
    for (int t = 0; t < tab_count; ++t)
        if (TABS[t].count > 0 || TABS[t].rows == ABOUT)
            TABS[kept++] = TABS[t];
    tab_count = kept;
#endif
}

#endif
#if defined(ARES_UWP)
/* ARES_UWP: the Systems tab: every system of the registry, grouped by maker, each opening its own options page */
static const char *const SYSTEM_HELP =
    "The options of this system only: region, picture, sound, controllers and the hardware it emulates. Each system keeps its own, for all its games; the ones marked with a clock apply when a game starts.";

static void build_system_rows(Tab *tab)
{
    static const struct
    {
        const char *maker;
        const char *ids;
    } MAKERS[] = {
        {"Nintendo", " fc fds sfc gb gbc gba "}, {"Sega", " sg sc3000 ms gg md mcd 32x "}, {"NEC", " pce sgx pcecd "},
        {"Sony", " ps1 "},                       {"SNK", " ng ngp ngpc "},                 {"Bandai", " ws wsc pcv2 "},
        {"Other systems", " a26 cv myvision msx msx2 spec "},
    };
    int n_sys = 0;
    const SwanSystem *list = swanui_systems(&n_sys);
    Row *rows = malloc(sizeof(Row) * (size_t)(n_sys > 0 ? n_sys : 1));
    if (!rows)
        return; /* out of memory: the placeholder stays */
    int n = 0;
    for (size_t m = 0; m < sizeof(MAKERS) / sizeof(MAKERS[0]); ++m)
    {
        bool first = true;
        for (int i = 0; i < n_sys && n < n_sys; ++i)
        {
            char key[24];
            snprintf(key, sizeof(key), " %s ", list[i].id);
            if (!strstr(MAKERS[m].ids, key))
                continue;
            Row r = {first ? MAKERS[m].maker : NULL, list[i].name, SYSTEM_HELP, K_ACTION, APPLY_NOW, SP_SYSTEM, false,
                     NO_FIELD, NULL, 0, i};
            rows[n++] = r;
            first = false;
        }
    }
    tab->rows = rows;
    tab->count = n;
}

static bool is_systems_tab(const Tab *t)
{
    return t->count > 0 && t->rows[0].special == SP_SYSTEM;
}
#endif
static void ensure_rows(void)
{
    if (ss_rows_built)
        return;
    ss_rows_built = true;
    for (int t = 0; t < TAB_COUNT; ++t)
    {
        const Row *base = TABS[t].rows;
        int count = TABS[t].count;
#if defined(ARES_UWP)
        if (base == SYSTEMS)
        {
            build_system_rows(&TABS[t]);
            continue;
        }
#endif
        if (base == SYSTEM)
        {
            static const int cats[] = {SSC_CONSOLE, SSC_ADVANCED};
            add_swanstation_rows(&TABS[t], base, count, cats, 2);
        }
        else if (base == GRAPHICS)
        {
            static const int cats[] = {SSC_ENHANCEMENT};
            add_swanstation_rows(&TABS[t], base, count, cats, 1);
        }
        else if (base == DISPLAY)
        {
            static const int cats[] = {SSC_DISPLAY};
            add_swanstation_rows(&TABS[t], base, count, cats, 1);
        }
        else if (base == CONTROLS)
        {
            static const int cats[] = {SSC_PORT};
            add_swanstation_rows(&TABS[t], base, count, cats, 1);
        }
    }
#if defined(SWANUI_XBOX)
    xbox_filter_tabs(); /* SWANUI_XBOX (S5) */
#endif
}

/* ---------------------------------------------------------------- state */

static struct
{
    int tab, cursor;
    bool game_scope; /* editing this game's own settings */
    bool remap;      /* the button-mapping page */
    bool players;    /* the player-order page */
    bool who;        /* the profiles page */
    int who_cursor;
    float who_y;
    int order[SwanStationPS5_MAX_PADS], assigned; /* controllers chosen so far, in player order */
    uint32_t pad_prev[SwanStationPS5_MAX_PADS];
    int remap_cursor;
    float tab_x, tab_w, sel_y, scroll, scope_x, remap_y;
    float switch_t[MAX_ROWS];
    int drive; /* the drive shown in Drives */
    bool sys;  /* ARES_UWP: a system's options page (Systems tab) is up */
} S;

/* The game whose own settings the "This game" side edits: the running one, or the shelf's (Settings opened from the shelf) */
static const Game *scope_game(void)
{
    return app.game ? app.game : app.settings_game;
}

/* The BIOS rows list what the folder holds: a file added since is appended to the choices */
static void refresh_bios_rows(void)
{
    ensure_rows();
    bios_choices_scan(app.paths.bios);
    for (int t = 0; t < TAB_COUNT; ++t)
        for (int k = 0; k < TABS[t].count; ++k)
        {
            Row *r = (Row *)&TABS[t].rows[k];
            if (!is_swanstation_row(r))
                continue;
            int i = (r->offset - (int)offsetof(Settings, ss_opt)) / (int)sizeof(int);
            if (ss_opt_is_bios(i))
                r->count = ss_opt_count(i);
        }
}

static void sync_framegen_rows(void)
{
    for (int t = 0; t < TAB_COUNT; ++t)
        for (int k = 0; k < TABS[t].count; ++k)
        {
            Row *r = (Row *)&TABS[t].rows[k];
            if (r->offset != (int)offsetof(Settings, framegen))
                continue;
            r->count = app.global.video_out == 1 ? 3 : 2;
            if (app.global.framegen >= r->count)
                app.global.framegen = r->count - 1;
            if (app.settings.framegen >= r->count)
                app.settings.framegen = r->count - 1;
        }
}

void settings_opened(void)
{
    ensure_rows();
    sync_framegen_rows();
    refresh_bios_rows();
    S.cursor = -1;
    S.remap = false;
    S.players = false;
    S.who = false;
    S.sys = false;
    S.game_scope = scope_game() != NULL && app.game_has_own;
    S.tab_x = 0;
    S.sel_y = -1;
    S.scroll = 0;
}

static const Tab *tab(void)
{
    ensure_rows();
    return &TABS[S.tab];
}

static bool selectable(const Row *r)
{
#if defined(SWANUI_XBOX)
    if (tab()->rows == ABOUT)
        return true; /* SWANUI_XBOX (S5): About's rows are read one by one, each with its help */
#endif
    return r->kind != K_INFO || r->special == SP_DRIVES || r->special == SP_MENU_COMBO;
}

static void first_selectable(void)
{
    S.cursor = -1;
    for (int i = 0; i < tab()->count; ++i)
        if (selectable(&tab()->rows[i]))
        {
            S.cursor = i;
            break;
        }
#if defined(ARES_UWP)
    /* ARES_UWP: in a game, the Systems tab starts on the running game's system */
    if (app.game && app.game->system[0] && is_systems_tab(tab()))
    {
        const int index = swanui_system_index(app.game->system);
        for (int i = 0; i < tab()->count; ++i)
            if (tab()->rows[i].base == index)
                S.cursor = i;
    }
#endif
}

/* The settings a row edits: this game's own, or the console-wide ones. */
static Settings *target(const Row *r)
{
    return (!r->global_only && S.game_scope && scope_game()) ? &app.settings : &app.global;
}

/* "Load HD textures" is the two core options "Enable VRAM Write Texture Replacement" and "Preload Texture
 * Replacements" together (both stay in System): on turns both on, off turns both off. */
static const char *const HDTEX_KEYS[] = {"odin_TextureReplacements_EnableVRAMWriteReplacements",
                                         "odin_TextureReplacements_PreloadTextures"};

static int ss_index(const char *key)
{
    for (int i = 0; i < SS_OPT_COUNT; ++i)
        if (strcmp(SS_OPTS[i].key, key) == 0)
            return i;
    return -1;
}

static int ss_true_index(int i)
{
    for (int v = 0; v < SS_OPTS[i].count; ++v)
        if (strcmp(SS_OPTS[i].values[v], "true") == 0)
            return v;
    return 0;
}

static bool hdtex_on(const Settings *t)
{
    int i = ss_index(HDTEX_KEYS[0]);
    return i >= 0 && t->ss_opt[i] == ss_true_index(i);
}

static void hdtex_set(Settings *t, bool on)
{
    for (size_t k = 0; k < sizeof(HDTEX_KEYS) / sizeof(HDTEX_KEYS[0]); ++k)
    {
        int i = ss_index(HDTEX_KEYS[k]);
        if (i < 0)
            continue;
        int yes = ss_true_index(i);
        t->ss_opt[i] = on ? yes : (yes + 1) % SS_OPTS[i].count;
    }
}

/* Turning the widescreen hack on sets the aspect ratio to 16:9 (remembering the one it had); turning it off puts
 * that one back, or 4:3 when it is not known */
static void widescreen_follow(Settings *t, const Row *r, int before)
{
    int w = ss_index("odin_GPU_WidescreenHack"), a = ss_index("odin_Display_AspectRatio");
    if (w < 0 || a < 0 || r->offset != (int)(offsetof(Settings, ss_opt) + (size_t)w * sizeof(int)))
        return;
    if (t->ss_opt[w] == before) /* the hack did not change (Triangle on a default, the same value again) */
        return;
    bool on = t->ss_opt[w] == ss_true_index(w);
    int v43 = -1, v169 = -1;
    for (int v = 0; v < SS_OPTS[a].count; ++v)
    {
        if (strcmp(SS_OPTS[a].values[v], "4:3") == 0)
            v43 = v;
        if (strcmp(SS_OPTS[a].values[v], "16:9") == 0)
            v169 = v;
    }
    if (v43 < 0 || v169 < 0)
        return;
    if (on)
    {
        if (t->aspect_before_wide < 0) /* what it was, even when it was already 16:9 */
            t->aspect_before_wide = t->ss_opt[a];
        t->ss_opt[a] = v169;
    }
    else
    {
        t->ss_opt[a] = t->aspect_before_wide >= 0 ? t->aspect_before_wide : v43;
        t->aspect_before_wide = -1;
    }
}

static int ac_opt(void)
{
    static int idx = -2;
    if (idx == -2)
    {
        idx = -1;
        for (int i = 0; i < SS_OPT_COUNT; ++i)
            if (strcmp(SS_OPTS[i].key, "odin_Controller_AnalogCombo") == 0)
                idx = i;
    }
    return idx;
}

static bool value_conflicts(const Row *r, int v)
{
    const int a = ac_opt();
    if (a < 0)
        return false;
    if (r->offset == (int)offsetof(Settings, menu_combo))
        return v == 1 && (app.global.ss_opt[a] == 10 || app.settings.ss_opt[a] == 10);
    if (r->offset == (int)(offsetof(Settings, ss_opt) + (size_t)a * sizeof(int)))
        return v == 10 && app.global.menu_combo == 1;
    return false;
}

/* The label that shows a value of the setting, and the value a label stands for. */
static int label_of(const Row *r, int value)
{
    if (r->ivalues)
    {
        /* ARES_UWP: the label of the nearest value (a hand-edited settings file may hold any number in range) */
        int best = 0;
        for (int k = 1; k < r->count; ++k)
            if (abs(r->ivalues[k] - value) < abs(r->ivalues[best] - value))
                best = k;
        return best;
    }
    for (int k = 0; r->order && k < r->count; ++k)
        if (r->order[k] == value)
            return k;
#if defined(SWANUI_XBOX)
    if (r->order)
        return 0; /* SWANUI_XBOX (S5): a value the Xbox list does not offer (a shader DuckStation lacks) shows as Off */
#endif
    return value;
}

static int value_of(const Row *r, int label)
{
    if (r->ivalues) /* ARES_UWP: nits */
        return r->ivalues[label >= 0 && label < r->count ? label : 0];
    return r->order && label >= 0 && label < r->count ? r->order[label] : label;
}

static int read_value(const Row *r)
{
    switch (r->special)
    {
    case SP_HDTEX: return hdtex_on(target(r)) ? 1 : 0;
    case SP_HARDCORE: return ra_hardcore();
    case SP_UNLOCK: return app.unlock_setting;
    default: break;
    }
    if (r->offset < 0)
        return 0;
    const char *base = (const char *)target(r) + r->offset;
    return r->is_bool ? (*(const bool *)base ? 1 : 0) : label_of(r, *(const int *)base - r->base);
}

/* A choice or switch that is the same for every game, seen from "This game": it shows "Global setting." and can't be
 * changed there (it is changed in "All games") */
static bool row_locked(const Row *r)
{
    return r->global_only && S.game_scope && scope_game() && (r->kind == K_CHOICE || r->kind == K_TOGGLE);
}

static void write_value(const Row *r, int v)
{
    if (row_locked(r) || value_conflicts(r, v))
        return;
    switch (r->special)
    {
    case SP_HDTEX:
    {
        Settings *t = target(r);
        hdtex_set(t, v != 0);
        if (t == &app.global && (!scope_game() || !app.game_has_own))
            hdtex_set(&app.settings, v != 0);
        if (app.game)
            host_apply_settings(&app.settings);
        return;
    }
    case SP_HARDCORE:
        if (!ra_user()[0])
        {
            app_toast("Sign in first: use Sign in under Achievements");
            return;
        }
        ra_set_hardcore(v != 0); /* turning it on restarts the game */
        if (v)
            cheats_clear(&app.cheats);
        return;
    case SP_UNLOCK:
        app.unlock_setting = v != 0;
        plat_set_unlock_disabled(!app.unlock_setting);
        return;
    default:
        break;
    }
    if (r->offset < 0)
        return;
    Settings *t = target(r);
    char *base = (char *)t + r->offset;
    int was_t = r->is_bool ? 0 : *(int *)base, was_eff = r->is_bool ? 0 : *(int *)((char *)&app.settings + r->offset);
    if (r->is_bool)
        *(bool *)base = v != 0;
    else
        *(int *)base = value_of(r, v) + r->base;
    /* console-wide edits reach the running game unless it has its own settings */
    if (t == &app.global && (!scope_game() || !app.game_has_own || r->global_only))
    {
        char *eff = (char *)&app.settings + r->offset;
        if (r->is_bool)
            *(bool *)eff = v != 0;
        else
            *(int *)eff = value_of(r, v) + r->base;
    }
    widescreen_follow(t, r, was_t);
    if (t == &app.global && (!scope_game() || !app.game_has_own || r->global_only))
        widescreen_follow(&app.settings, r, was_eff);
    if (r->special == SP_LANGUAGE)
        i18n_set(app.global.language);
    if (r->special == SP_THEME)
        theme_apply(app.global.theme);
#if defined(ARES_UWP)
    if (r->special == SP_HDR)
        app_apply_hdr(); /* ARES_UWP: the display follows at once */
#endif
    if (r->special == SP_REMOTE)
        remote_update(app.global.remote);
    if (r->special == SP_SOUND)
        sfx_configure(app.global.ui_sound, (app.global.ui_volume + 1) * 25);
    if (app.game)
    {
        host_apply_settings(&app.settings);
        controls_reconfigure(app.game, &app.settings);
    }
}

static Settings *mc_targets(Settings *list[2])
{
    const Row *row = NULL;
    for (size_t i = 0; i < sizeof(DISPLAY) / sizeof(DISPLAY[0]); ++i)
        if (DISPLAY[i].offset == (int)offsetof(Settings, manual_crop))
            row = &DISPLAY[i];
    if (!row)
        return NULL;
    list[0] = target(row);
    list[1] = NULL;
    if (list[0] == &app.global && (!scope_game() || !app.game_has_own || row->global_only))
        list[1] = &app.settings;
    return list[0];
}

static int mc_clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void manual_crop_fix(void)
{
    Settings *list[2];
    if (!mc_targets(list))
        return;
    for (int i = 0; i < 2 && list[i]; ++i)
    {
        list[i]->mc_w = list[i]->mc_h = (list[i]->mc_w + list[i]->mc_h + 1) / 2;
        list[i]->mc_x = 0;
    }
}

void manual_crop_edit(int grow, int dy, bool reset)
{
    Settings *list[2];
    if (!mc_targets(list))
        return;
    for (int i = 0; i < 2 && list[i]; ++i)
    {
        Settings *s = list[i];
        if (reset)
        {
            s->mc_w = s->mc_h = 100;
            s->mc_x = s->mc_y = 0;
            continue;
        }
        s->mc_w = s->mc_h = mc_clamp(s->mc_w + grow, 25, 150);
        s->mc_y = mc_clamp(s->mc_y + dy, -500, 500);
    }
}

static const char *value_label(const Row *r, char *buf, size_t size)
{
    int v = read_value(r);
    switch (r->special)
    {
    case SP_LANGUAGE: return i18n_name(v);
    case SP_ACCOUNT:
        if (!ra_user()[0])
            return tr("Not signed in");
        snprintf(buf, size, "%s%s", ra_user(), ra_signed_in() ? "" : tr(" (not signed in)"));
        return buf;
#if defined(ARES_UWP)
    case SP_VERSION: return "v" SwanStationPS5_SHOWN_VERSION " (Xbox)"; /* ARES_UWP: the ares release */
#elif defined(SWANUI_XBOX)
    case SP_VERSION: return SwanStationPS5_VERSION " (Xbox)"; /* SWANUI_XBOX (S5) */
#else
    case SP_VERSION: return SwanStationPS5_VERSION;
#endif
    case SP_BIOS: app_describe_bios(buf, size); return buf;
    case SP_GAMES: snprintf(buf, size, "%d", app.library.count); return buf;
#if defined(SWANUI_XBOX)
    case SP_DATA:
    {
        /* SWANUI_XBOX (S5): from LocalState on, the part Device Portal's file explorer shows */
        const char *ls = strstr(app.paths.root, "LocalState");
        return ls ? ls : app.paths.root;
    }
#else
    case SP_DATA: return app.paths.root;
#endif
    case SP_DRIVES:
    {
        if (!app.drive_count)
            return tr("None");
        if (S.drive >= app.drive_count)
            S.drive = 0;
        return app.drive_text[S.drive];
    }
#if defined(ARES_UWP)
    case SP_CREDITS: return "Odin: see the credits file in the package"; /* ARES_UWP: the emulator's authors; the interface's in the help */
#elif defined(TSUGARU_UWP)
    case SP_CREDITS: return "Soji Yamakawa (CaptainYS)"; /* TSUGARU_UWP: the emulator's author; the interface's is in the help */
#else
    case SP_CREDITS: return "AngelXex";
#endif
#if defined(SWANUI_XBOX)
    case SP_CORE: return swanui_core_version(); /* SWANUI_XBOX (S5) */
#endif
    case SP_HOTKEYS: return tr("{Touchpad} + {R2} / {L2}");
#if defined(ARES_UWP)
    case SP_MENU_COMBO: return "{L3} + {R3}   {L1} + {R1}   {Touchpad} + {Options}";
    case SP_SYSTEM:
    {
        int n = 0;
        const SwanSystem *list = swanui_systems(&n);
        if (r->base < 0 || r->base >= n)
            return "";
        if (host_sysopt_running(list[r->base].id))
            return tr("Playing now");
        const int count = host_sysopt_count(list[r->base].id);
        snprintf(buf, size, tr(count == 1 ? "%d option" : "%d options"), count);
        return buf;
    }
#endif
#if defined(SWANUI_XBOX) /* SWANUI_XBOX: the picked folder */
    case SP_USBFOLDER: return swanui_xbox_games_root()[0] ? swanui_xbox_games_root() : tr("Off");
#else
    case SP_USBFOLDER: return app.global.usb_folder[0] ? app.global.usb_folder : tr("Off");
#endif
    case SP_NFSHARES: return app.global.nfs_shares[0] ? app.global.nfs_shares : tr("Off");
    case SP_UPDATE:
        switch (update_state())
        {
        case UPDATE_CHECKING: return tr("Checking...");
        case UPDATE_NONE: return tr("Up to date");
        case UPDATE_AVAILABLE: snprintf(buf, size, tr("%s available: install"), update_version()); return buf;
        case UPDATE_INSTALLING: return tr("Installing...");
        case UPDATE_INSTALLED: return tr("Installed: restart Odin");
        case UPDATE_FAILED: return update_message();
        default: return SwanStationPS5_VERSION;
        }
    default: break;
    }
    if (!r->labels || v < 0 || v >= r->count)
        return "";
    return tr(r->labels[v]);
}

static void default_value(const Row *r)
{
    if (r->offset < 0)
        return;
    Settings d;
    config_defaults(&d);
    const char *base = (const char *)&d + r->offset;
    write_value(r, r->is_bool ? (*(const bool *)base ? 1 : 0) : label_of(r, *(const int *)base - r->base));
}

/* ---------------------------------------------------------------- the button-mapping page */

typedef struct
{
    int button; /* BTN_* on the controller */
    int glyph;
    const char *name;
    float x, y; /* on the controller drawing, relative to its centre */
} PadButton;

#if defined(SWANUI_XBOX)
/* SWANUI_XBOX (S5): the Xbox Wireless Controller: A B X Y where Cross Circle Square Triangle were, the left stick high
 * and the D-pad low, Menu (the PS1 Start) in the middle. View is not remappable (it is the shortcuts' key). */
static const PadButton PAD[] = {
    {BTN_CROSS, GLYPH_CROSS, "A", 230, 46},
    {BTN_CIRCLE, GLYPH_CIRCLE, "B", 290, -14},
    {BTN_SQUARE, GLYPH_SQUARE, "X", 170, -14},
    {BTN_TRIANGLE, GLYPH_TRIANGLE, "Y", 230, -74},
    {BTN_UP, GLYPH_UP, "Up", -110, 44},
    {BTN_DOWN, GLYPH_DOWN, "Down", -110, 140},
    {BTN_LEFT, GLYPH_LEFT, "Left", -158, 92},
    {BTN_RIGHT, GLYPH_RIGHT, "Right", -62, 92},
    {BTN_L1, GLYPH_L1, "LB", -230, -168},
    {BTN_R1, GLYPH_R1, "RB", 230, -168},
    {BTN_L2, GLYPH_L2, "LT", -230, -226},
    {BTN_R2, GLYPH_R2, "RT", 230, -226},
    {BTN_L3, GLYPH_L3, "LS (left stick)", -230, -14},
    {BTN_R3, GLYPH_R3, "RS (right stick)", 110, 100},
    {BTN_START, GLYPH_START, "Menu", 70, -40},
};
#else
static const PadButton PAD[] = {
    {BTN_CROSS, GLYPH_CROSS, "Cross", 230, 46},
    {BTN_CIRCLE, GLYPH_CIRCLE, "Circle", 290, -14},
    {BTN_SQUARE, GLYPH_SQUARE, "Square", 170, -14},
    {BTN_TRIANGLE, GLYPH_TRIANGLE, "Triangle", 230, -74},
    {BTN_UP, GLYPH_UP, "Up", -230, -70},
    {BTN_DOWN, GLYPH_DOWN, "Down", -230, 42},
    {BTN_LEFT, GLYPH_LEFT, "Left", -286, -14},
    {BTN_RIGHT, GLYPH_RIGHT, "Right", -174, -14},
    {BTN_L1, GLYPH_L1, "L1", -230, -168},
    {BTN_R1, GLYPH_R1, "R1", 230, -168},
    {BTN_L2, GLYPH_L2, "L2", -230, -226},
    {BTN_R2, GLYPH_R2, "R2", 230, -226},
    {BTN_L3, GLYPH_L3, "L3 (left stick)", -110, 110},
    {BTN_R3, GLYPH_R3, "R3 (right stick)", 110, 110},
    {BTN_START, GLYPH_START, "OPTIONS", 214, -122},
};
#endif
#define PAD_COUNT (int)(sizeof(PAD) / sizeof(PAD[0]))

/* What the PS1 controller has, in the order Left/Right cycles through. */
static const struct
{
    int button, glyph;
    const char *name;
} PS1[] = {
    {BTN_CROSS, GLYPH_CROSS, "Cross"},       {BTN_CIRCLE, GLYPH_CIRCLE, "Circle"},
    {BTN_SQUARE, GLYPH_SQUARE, "Square"},    {BTN_TRIANGLE, GLYPH_TRIANGLE, "Triangle"},
    {BTN_UP, GLYPH_UP, "Up"},                {BTN_DOWN, GLYPH_DOWN, "Down"},
    {BTN_LEFT, GLYPH_LEFT, "Left"},          {BTN_RIGHT, GLYPH_RIGHT, "Right"},
    {BTN_L1, GLYPH_L1, "L1"},                {BTN_R1, GLYPH_R1, "R1"},
    {BTN_L2, GLYPH_L2, "L2"},                {BTN_R2, GLYPH_R2, "R2"},
    {BTN_L3, GLYPH_L3, "L3"},                {BTN_R3, GLYPH_R3, "R3"},
    {BTN_START, GLYPH_PS1_START, "Start"},   {BTN_SELECT, GLYPH_SELECT, "Select"},
    {-1, GLYPH_NONE, "Nothing"},
};
#define PS1_COUNT (int)(sizeof(PS1) / sizeof(PS1[0]))

static int ps1_index(int button)
{
    for (int i = 0; i < PS1_COUNT; ++i)
        if (PS1[i].button == button)
            return i;
    return PS1_COUNT - 1;
}

static Settings *map_target(void)
{
    return (S.game_scope && scope_game()) ? &app.settings : &app.global;
}

static void set_map(int button, int to)
{
    map_target()->button_map[button] = (int8_t)to;
    if (map_target() == &app.global && (!scope_game() || !app.game_has_own))
        app.settings.button_map[button] = (int8_t)to;
}

static const char *const PRESETS[] = {"Default", "Swap {Cross} and {Circle}", "Swap shoulders and triggers"};

static void apply_preset(int preset)
{
    for (int b = 0; b < 16; ++b)
        set_map(b, b);
    if (preset == 1)
    {
        set_map(BTN_CROSS, BTN_CIRCLE);
        set_map(BTN_CIRCLE, BTN_CROSS);
    }
    else if (preset == 2)
    {
        set_map(BTN_L1, BTN_L2);
        set_map(BTN_L2, BTN_L1);
        set_map(BTN_R1, BTN_R2);
        set_map(BTN_R2, BTN_R1);
    }
    char msg[96];
    snprintf(msg, sizeof(msg), tr("Mapping: %s"), tr(PRESETS[preset]));
    app_toast(msg);
}

static void draw_controller(float cx, float cy, int highlight)
{
    /* a DualSense, simplified: body, grips, touchpad, sticks */
    const uint32_t body = TH_PILL, edge = theme.light ? TH_DIVIDER : 0xff2f3a80u;
    draw_rrect(cx - 360, cy - 130, 720, 250, 120, edge);
    draw_rrect(cx - 354, cy - 124, 708, 238, 116, body);
    draw_circle(cx - 250, cy + 140, 92, edge);
    draw_circle(cx + 250, cy + 140, 92, edge);
    draw_circle(cx - 250, cy + 140, 86, body);
    draw_circle(cx + 250, cy + 140, 86, body);
    draw_rrect(cx - 354, cy - 60, 708, 170, 60, body);
#if defined(SWANUI_XBOX)
    /* SWANUI_XBOX (S5): no touchpad: View sits left of Menu and does its job (caption: entry 2 of the Xbox texts) */
    text_draw(cx, cy - 112, 18, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, tr("{Touchpad}: tap = {Select}, hold = menu"));
    draw_pad_glyph(GLYPH_TOUCHPAD, cx - 70, cy - 40, 44);
    static const float WELLS[2][2] = {{-230.0f, -14.0f}, {110.0f, 100.0f}}; /* left stick high, right stick low */
    for (int s = 0; s < 2; ++s)
    {
        draw_circle(cx + WELLS[s][0], cy + WELLS[s][1], 46, TH_BG);
        draw_circle(cx + WELLS[s][0], cy + WELLS[s][1], 34, edge);
    }
#else
    draw_rrect(cx - 110, cy - 118, 220, 110, 18, TH_BG_DEEP); /* touchpad */
    text_draw(cx, cy - 80, 18, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, tr("{Touchpad}: tap = {Select}, hold = menu"));
    for (int s = -1; s <= 1; s += 2)
    {
        draw_circle(cx + s * 110, cy + 110, 46, TH_BG);
        draw_circle(cx + s * 110, cy + 110, 34, edge);
    }
#endif
    for (int i = 0; i < PAD_COUNT; ++i)
    {
        float x = cx + PAD[i].x, y = cy + PAD[i].y;
        bool on = i == highlight;
        if (on)
        {
            float pulse = 0.6f + 0.4f * sinf((float)plat_ticks_us() / 1e6f * 6.0f);
            float w = pad_glyph_width((enum PadGlyph)PAD[i].glyph, 44) + 20;
            draw_rrect(x - w * 0.5f, y - 32, w, 64, 32, argb_alpha(TH_FOCUS, 0.35f * pulse));
            draw_rrect_outline(x - w * 0.5f, y - 32, w, 64, 32, 3, TH_FOCUS);
        }
        draw_pad_glyph((enum PadGlyph)PAD[i].glyph, x, y, 44);
    }
}

static void remap_page(uint32_t pressed)
{
    int before = S.remap_cursor;
    if (pressed & BIT(BTN_UP))
        S.remap_cursor = (S.remap_cursor + PAD_COUNT - 1) % PAD_COUNT;
    if (pressed & BIT(BTN_DOWN))
        S.remap_cursor = (S.remap_cursor + 1) % PAD_COUNT;
    if (S.remap_cursor != before)
        sfx_play(SFX_CLICK);
    const PadButton *pb = &PAD[S.remap_cursor];
    int current = ps1_index(map_target()->button_map[pb->button]);
    int step = (pressed & (BIT(BTN_RIGHT) | BIT(BTN_CROSS))) ? 1 : (pressed & BIT(BTN_LEFT)) ? -1 : 0;
    if (step)
    {
        current = (current + step + PS1_COUNT) % PS1_COUNT;
        set_map(pb->button, PS1[current].button);
        sfx_play(SFX_CLICK);
    }
    if (pressed & BIT(BTN_TRIANGLE))
    {
        set_map(pb->button, pb->button);
        sfx_play(SFX_CLICK);
    }
    static int preset;
    if (pressed & BIT(BTN_SQUARE))
    {
        preset = (preset + 1) % 3;
        apply_preset(preset);
        sfx_play(SFX_SELECT);
    }
    if (pressed & BIT(BTN_CIRCLE))
    {
        S.remap = false;
        app_save_settings();
        sfx_play(SFX_BACK);
        return;
    }

    draw_controller(800, 560, S.remap_cursor);

    /* the list */
    const float x = 1260, w = 600, top = 170, row_h = 50;
    draw_rrect(x - 16, top - 16, w + 32, PAD_COUNT * row_h + 32, TH_RADIUS, TH_CARD);
    float target_y = top + S.remap_cursor * row_h;
    if (S.remap_y == 0)
        S.remap_y = target_y;
    anim_approach(&S.remap_y, target_y, app.dt, TH_SNAP);
    draw_rrect(x - 4, S.remap_y, w + 8, row_h - 4, 10, TH_ROW_SELECTED);
    for (int i = 0; i < PAD_COUNT; ++i)
    {
        float y = top + i * row_h;
        const PadButton *b = &PAD[i];
        int to = ps1_index(map_target()->button_map[b->button]);
        bool changed = map_target()->button_map[b->button] != b->button;
        draw_pad_glyph((enum PadGlyph)b->glyph, x + 52, y + row_h * 0.45f, 28);
        text_draw(x + 108, y + 9, 22, FONT_REGULAR, TH_TEXT, ALIGN_LEFT, tr(b->name));
        icon_draw(ICON_ARROWS_EXCHANGE, x + 318, y + 9, 26, changed ? TH_FOCUS : TH_TEXT_DIM);
#if defined(SWANUI_XBOX)
        draw_set_ps_glyphs(true); /* SWANUI_XBOX (S5): the PS1 button this one presses keeps its PlayStation picture */
        draw_pad_glyph((enum PadGlyph)PS1[to].glyph, x + 408, y + row_h * 0.45f, 28);
        draw_set_ps_glyphs(false);
#else
        draw_pad_glyph((enum PadGlyph)PS1[to].glyph, x + 408, y + row_h * 0.45f, 28);
#endif
        text_draw(x + 464, y + 9, 22, FONT_REGULAR, changed ? TH_FOCUS : TH_TEXT_SOFT, ALIGN_LEFT,
                  tr(PS1[to].name));
    }

    static const int glyphs[] = {GLYPH_LEFT, GLYPH_TRIANGLE, GLYPH_SQUARE, GLYPH_CIRCLE};
    static const char *const labels[] = {"Change", "Reset button", "Presets", "Back"};
    app_draw_hints(glyphs, labels, 4, NULL);
}

/* ---------------------------------------------------------------- the player-order page */

/* Each controller presses Cross in turn: the first is player 1, and so on. */
static void players_page(uint32_t pressed)
{
    int current[SwanStationPS5_MAX_PADS], connected = 0;
    plat_player_order(current);
    for (int k = 0; k < SwanStationPS5_MAX_PADS; ++k)
        connected += app.pads[k].connected;
    for (int k = 0; k < SwanStationPS5_MAX_PADS; ++k)
    {
        uint32_t now = app.pads[k].buttons, newly = now & ~S.pad_prev[k];
        S.pad_prev[k] = now;
        if (!(newly & BIT(BTN_CROSS)) || !app.pads[k].connected)
            continue;
        int controller = current[k];
        bool taken = false;
        for (int i = 0; i < S.assigned; ++i)
            taken |= S.order[i] == controller;
        if (!taken && S.assigned < SwanStationPS5_MAX_PADS)
        {
            S.order[S.assigned++] = controller;
            sfx_play(SFX_SELECT);
        }
    }
    if (S.assigned > 0 && S.assigned >= connected)
    {
        /* the controllers that aren't here take the places left */
        int order[SwanStationPS5_MAX_PADS], n = S.assigned;
        memcpy(order, S.order, sizeof(order));
        for (int c = 0; c < SwanStationPS5_MAX_PADS && n < SwanStationPS5_MAX_PADS; ++c)
        {
            bool used = false;
            for (int i = 0; i < n; ++i)
                used |= order[i] == c;
            if (!used)
                order[n++] = c;
        }
        plat_set_player_order(order);
        S.players = false;
        app_toast("Player order set");
        return;
    }
    if (pressed & BIT(BTN_SQUARE))
    {
        static const int standard[SwanStationPS5_MAX_PADS] = {0, 1, 2, 3};
        plat_set_player_order(standard);
        S.players = false;
        app_toast("Players back in the console's order");
        sfx_play(SFX_SELECT);
        return;
    }
    if (pressed & BIT(BTN_CIRCLE))
    {
        S.players = false;
        sfx_play(SFX_BACK);
        return;
    }

    static const uint32_t colours[SwanStationPS5_MAX_PADS] = {0xff2050ffu, 0xffff2030u, 0xff20d040u, 0xffff40c0u};
    const float w = 900, x = (plat_width() - w) * 0.5f, top = 250, row_h = 120;
    text_draw(plat_width() * 0.5f, 170, 30, FONT_BOLD, TH_TEXT, ALIGN_CENTER, tr("Player order"));
    draw_rrect(x - 16, top - 16, w + 32, SwanStationPS5_MAX_PADS * row_h + 32, TH_RADIUS, TH_CARD);
    for (int i = 0; i < SwanStationPS5_MAX_PADS; ++i)
    {
        float y = top + i * row_h;
        bool done = i < S.assigned, next = i == S.assigned;
        if (next)
            draw_rrect(x, y + 6, w, row_h - 12, TH_RADIUS_SMALL, TH_ROW_SELECTED);
        draw_circle(x + 56, y + row_h * 0.5f, 22, done || next ? colours[i] : argb_alpha(colours[i], 0.35f));
        char label[32];
        snprintf(label, sizeof(label), tr("Player %d"), i + 1);
        text_draw(x + 100, y + 26, 28, FONT_BOLD, done || next ? TH_TEXT : TH_TEXT_DIM, ALIGN_LEFT, label);
        const char *who = NULL;
        if (done)
            for (int k = 0; k < SwanStationPS5_MAX_PADS; ++k)
                if (current[k] == S.order[i])
                    who = plat_pad_name(k);
        text_draw_fit(x + 100, y + 66, 22, FONT_REGULAR, next ? TH_FOCUS : TH_TEXT_SOFT, ALIGN_LEFT, w - 140,
                      done ? (who ? who : tr("Controller")) : next ? tr("Press {Cross} on this player's controller") : "");
    }
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_SQUARE, GLYPH_CIRCLE};
    static const char *const labels[] = {"Choose", "Console's order", "Back"};
    app_draw_hints(glyphs, labels, 3, NULL);
}

/* ---------------------------------------------------------------- the profiles page */

/* The profiles, then a new one for each signed-in PS5 user who hasn't got
 * one, then a numbered player. */
static int who_rows(char adds[][PROFILE_NAME_LEN], int *add_count)
{
    *add_count = 0;
    for (int k = 0; k < SwanStationPS5_MAX_PADS && *add_count < 5; ++k)
    {
        const char *name = app.pads[k].connected ? plat_pad_name(k) : NULL;
        if (!name || !name[0] || profiles_find(name) >= 0)
            continue;
        bool dup = false;
        for (int i = 0; i < *add_count; ++i)
            dup |= !strcmp(adds[i], name);
        if (!dup)
            str_copy(adds[(*add_count)++], PROFILE_NAME_LEN, name);
    }
    for (int n = 2; n < 20 && *add_count < 6; ++n)
    {
        char name[PROFILE_NAME_LEN];
        snprintf(name, sizeof(name), tr("Player %d"), n);
        if (profiles_find(name) < 0)
        {
            str_copy(adds[(*add_count)++], PROFILE_NAME_LEN, name);
            break;
        }
    }
    return profiles_count() + *add_count;
}

static void who_page(uint32_t pressed)
{
    char adds[6][PROFILE_NAME_LEN];
    int add_count, rows = who_rows(adds, &add_count), have = profiles_count();
    int before = S.who_cursor;
    if (pressed & BIT(BTN_UP))
        S.who_cursor = (S.who_cursor + rows - 1) % rows;
    if (pressed & BIT(BTN_DOWN))
        S.who_cursor = (S.who_cursor + 1) % rows;
    if (S.who_cursor >= rows)
        S.who_cursor = rows - 1;
    if (S.who_cursor != before)
        sfx_play(SFX_CLICK);
    if (pressed & BIT(BTN_CIRCLE))
    {
        S.who = false;
        sfx_play(SFX_BACK);
        return;
    }
    if (pressed & BIT(BTN_CROSS))
    {
        if (app.game)
            app_toast("Quit the game first");
        else
        {
            int target = S.who_cursor < have ? S.who_cursor : profiles_add(adds[S.who_cursor - have]);
            if (target >= 0 && profiles_switch(target))
            {
                char msg[96];
                snprintf(msg, sizeof(msg), tr("Playing as %s"), profiles_name(target));
                app_toast(msg);
                sfx_play(SFX_SELECT);
                S.who = false;
                return;
            }
        }
    }
    if ((pressed & BIT(BTN_SQUARE)) && S.who_cursor > 0 && S.who_cursor < have)
    {
        if (profiles_remove(S.who_cursor))
        {
            app_toast("Profile taken off the list (its files are kept)");
            S.who_cursor = 0;
        }
        else
            app_toast("Switch to another profile first");
    }

    const float w = 900, x = (plat_width() - w) * 0.5f, top = 230, row_h = 76;
    text_draw(plat_width() * 0.5f, 150, 30, FONT_BOLD, TH_TEXT, ALIGN_CENTER, tr("Who's playing"));
    draw_rrect(x - 16, top - 16, w + 32, rows * row_h + 24, TH_RADIUS, TH_CARD);
    float target_y = top + S.who_cursor * row_h;
    if (S.who_y == 0)
        S.who_y = target_y;
    anim_approach(&S.who_y, target_y, app.dt, TH_SNAP);
    draw_rrect(x, S.who_y, w, row_h - 8, TH_RADIUS_SMALL, TH_ROW_SELECTED);
    for (int i = 0; i < rows; ++i)
    {
        float y = top + i * row_h;
        bool add = i >= have, here = i == profiles_current();
        icon_draw(add ? ICON_PLUS : ICON_USER, x + 24, y + 18, 32, add ? TH_TEXT_DIM : TH_FOCUS);
        char label[PROFILE_NAME_LEN + 32];
        if (add)
            snprintf(label, sizeof(label), tr("New profile: %s"), adds[i - have]);
        else
            str_copy(label, sizeof(label), profiles_name(i));
        text_draw_fit(x + 76, y + 19, 26, here ? FONT_BOLD : FONT_REGULAR, add ? TH_TEXT_SOFT : TH_TEXT, ALIGN_LEFT,
                      w - 300, label);
        if (here)
            text_draw(x + w - 24, y + 22, 22, FONT_REGULAR, TH_GOOD, ALIGN_RIGHT, tr("Playing now"));
    }
    static const int glyphs[] = {GLYPH_CROSS, GLYPH_SQUARE, GLYPH_CIRCLE};
    static const char *const labels[] = {"Switch", "Remove", "Back"};
    app_draw_hints(glyphs, labels, 3, NULL);
}

/* ---------------------------------------------------------------- help panel */

/* Wrapped text; returns the height used. With draw off, only the height. */
static float wrap_text(float x, float y, float w, float size, uint32_t argb, const char *s, bool draw)
{
    char line[256];
    float yy = y;
    while (*s)
    {
        size_t best = 0, i = 0;
        for (;;)
        {
            size_t j = i;
            while (s[j] && s[j] != ' ')
                ++j;
            size_t n = j < sizeof(line) - 1 ? j : sizeof(line) - 1;
            memcpy(line, s, n);
            line[n] = '\0';
            if (best > 0 && text_width(size, FONT_REGULAR, line) > w)
                break;
            best = n;
            if (!s[j])
                break;
            i = j + 1;
        }
        if (best == 0)
            best = strlen(s) < sizeof(line) - 1 ? strlen(s) : sizeof(line) - 1;
        memcpy(line, s, best);
        line[best] = '\0';
        if (draw)
            text_draw(x, yy, size, FONT_REGULAR, argb, ALIGN_LEFT, line);
        yy += size * 1.45f;
        s += best;
        while (*s == ' ')
            ++s;
    }
    return yy - y;
}

static float draw_wrapped(float x, float y, float w, float size, uint32_t argb, const char *s)
{
    return wrap_text(x, y, w, size, argb, s, true);
}

/* Wrapped text that shrinks until it fits in `room` pixels of height (down to 16), so a long help stays inside its card. */
static float draw_wrapped_fit(float x, float y, float w, float size, uint32_t argb, const char *s, float room)
{
    while (size > 16.0f && wrap_text(x, y, w, size, argb, s, false) > room)
        size -= 1.0f;
    return wrap_text(x, y, w, size, argb, s, true);
}

/* Before and after for the upscale filter: a tiny sprite, enlarged. */
static void draw_filter_preview(float x, float y, float size, int filter)
{
    enum { N = 24 };
    static PlatTexture *sharp, *smooth, *xbr;
    if (!sharp)
    {
        uint32_t src[N * N], big[N * N * 4];
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i)
            {
                float dx = i - 11.5f, dy = j - 11.5f;
                bool disc = dx * dx + dy * dy < 90.0f, stripe = (i + j) % 8 < 3 && !disc;
                src[j * N + i] = disc ? TH_FOCUS : stripe ? TH_GOLD : TH_PILL;
            }
        uint8_t rgba[N * N * 4 * 4];
        for (int k = 0; k < N * N; ++k)
        {
            rgba[k * 4 + 0] = (src[k] >> 16) & 0xff;
            rgba[k * 4 + 1] = (src[k] >> 8) & 0xff;
            rgba[k * 4 + 2] = src[k] & 0xff;
            rgba[k * 4 + 3] = 255;
        }
        sharp = plat_texture_create(rgba, N, N, false);
        smooth = plat_texture_create(rgba, N, N, true);
        xbr2x(src, N, N, N, big);
        for (int k = 0; k < N * N * 4; ++k)
        {
            rgba[k * 4 + 0] = (big[k] >> 16) & 0xff;
            rgba[k * 4 + 1] = (big[k] >> 8) & 0xff;
            rgba[k * 4 + 2] = big[k] & 0xff;
            rgba[k * 4 + 3] = 255;
        }
        xbr = plat_texture_create(rgba, N * 2, N * 2, true);
    }
    float half = (size - 16) * 0.5f;
    plat_draw_texture(sharp, x, y, half, half, 0xffffffffu, false);
    PlatTexture *after = filter == UPSCALE_XBR ? xbr : filter == UPSCALE_SMOOTH_PIXELS ? smooth : sharp;
    plat_draw_texture(after, x + half + 16, y, half, half, 0xffffffffu, false);
    text_draw(x + half * 0.5f, y + half + 10, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, tr("Original"));
    text_draw(x + half * 1.5f + 16, y + half + 10, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
              tr(FILTERS[filter]));
}

/* The project page as a QR code (assets/qr-github.png, one pixel per module,
 * made with segno: see tools/make_qr.py). Drawn on a white card
 * so phones read it on any background. */
static void draw_project_qr(float x, float y, float size)
{
    static PlatTexture *qr;
    static int modules;
    static bool tried;
    if (!tried)
    {
        tried = true;
        char path[SwanStationPS5_PATH_MAX];
        plat_asset_path(path, sizeof(path), "qr-github.png");
        int w = 0, h = 0, comp = 0;
        unsigned char *px = stbi_load(path, &w, &h, &comp, 4);
        if (px)
        {
            qr = plat_texture_create(px, w, h, false);
            modules = w;
            stbi_image_free(px);
        }
    }
    if (!qr || modules <= 0)
        return;
    int scale = (int)(size / modules);
    float side = (float)(scale * modules), pad = 14;
    draw_rrect(x - pad, y - pad, side + 2 * pad, side + 2 * pad, TH_RADIUS_SMALL, 0xffffffffu);
    plat_draw_texture(qr, x, y, side, side, 0xffffffffu, false);
}

/* Any text as a QR code (the phone page's address), made on the console. */
static void draw_url_qr(const char *text, float x, float y, float size)
{
    static char made_for[128];
    static PlatTexture *tex;
    static int modules;
    if (strcmp(made_for, text) != 0)
    {
        str_copy(made_for, sizeof(made_for), text);
        plat_texture_free(tex);
        tex = NULL;
        static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], temp[qrcodegen_BUFFER_LEN_MAX];
        if (qrcodegen_encodeText(text, temp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                 qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
        {
            int n = qrcodegen_getSize(qr), side = n + 4;
            uint8_t *px = malloc((size_t)side * side * 4);
            if (px)
            {
                for (int j = 0; j < side; ++j)
                    for (int i = 0; i < side; ++i)
                    {
                        bool dark = qrcodegen_getModule(qr, i - 2, j - 2);
                        uint8_t *p = &px[(j * side + i) * 4];
                        p[0] = dark ? 15 : 255, p[1] = dark ? 19 : 255, p[2] = dark ? 48 : 255, p[3] = 255;
                    }
                tex = plat_texture_create(px, side, side, false);
                modules = side;
                free(px);
            }
        }
    }
    if (!tex)
        return;
    int scale = (int)(size / modules);
    float side = (float)(scale * modules), pad = 14;
    draw_rrect(x - pad, y - pad, side + 2 * pad, side + 2 * pad, TH_RADIUS_SMALL, 0xffffffffu);
    plat_draw_texture(tex, x, y, side, side, 0xffffffffu, false);
}

static void draw_help(const Row *r)
{
    const float w = 476, x = plat_width() - TH_MARGIN - w, top = 236;
    draw_rrect(x, top, w, 724, TH_RADIUS, TH_CARD);
    if (!r && tab()->rows == ABOUT)
        r = &ABOUT[0]; /* nothing to select on About: describe SwanStationPS5 */
    if (!r)
        return;
    icon_draw(tab()->icon, x + 32, top + 32, 40, TH_FOCUS);
    text_draw_fit(x + 32, top + 92, 30, FONT_BOLD, TH_TEXT, ALIGN_LEFT, w - 64, tr(row_name(r)));
    float y = top + 150;
    if (r->offset == offsetof(Settings, upscale_filter))
    {
        draw_filter_preview(x + 32, y, w - 64, app.settings.upscale_filter);
        y += (w - 64 - 16) * 0.5f + 56;
    }
    /* the card ends above its notes: "Same for every game" (top + 646) and when it applies (top + 684) */
#if defined(SWANUI_XBOX)
    /* SWANUI_XBOX (S5): About keeps SwanStationPS5's project QR on Version and Created by; the credits get the card */
    const bool about_qr = tab()->rows == ABOUT && (r->special == SP_VERSION || r->special == SP_CREDITS);
    const float room = (about_qr ? top + 290 : r->global_only && S.game_scope && scope_game() ? top + 630 : top + 668) - y;
#else
    const float room = (r->global_only && S.game_scope && scope_game() ? top + 630 : top + 668) - y;
#endif
    y += draw_wrapped_fit(x + 32, y, w - 64, 24, TH_TEXT_SOFT, tr(row_help(r)), room) + 20;
#if defined(ARES_UWP)
    if (r->special == SP_HDR)
    {
        /* ARES_UWP: what the display does now (plat_hdr_state), under the help */
        const int state = plat_hdr_state();
        const char *line = state == 2   ? "Your display: HDR is on."
                           : state == 1 ? "Your display supports HDR; the picture is SDR now."
                                        : "Your display does not report HDR support.";
        icon_draw(ICON_DEVICE_TV, x + 32, y + 1, 26, TH_FOCUS);
        text_draw_fit(x + 70, y, 22, FONT_BOLD, TH_FOCUS, ALIGN_LEFT, w - 102, tr(line));
        y += 40;
    }
#endif
    static const char *const notes[] = {"Applies right away", "From the next game you start",
                                        "After Odin restarts"};
    if (r->special == SP_REMOTE)
    {
        const char *url = remote_address();
        if (app.global.remote && url[0])
        {
            float qs = 240, qx = x + (w - qs) * 0.5f, qy = top + 300;
            draw_url_qr(url, qx, qy, qs);
            text_draw(x + w * 0.5f, qy + qs + 30, 22, FONT_BOLD, TH_TEXT, ALIGN_CENTER,
                      tr("Scan with your phone"));
            text_draw(x + w * 0.5f, qy + qs + 64, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, url);
        }
        else if (app.global.remote)
            text_draw(x + 32, top + 360, 22, FONT_REGULAR, TH_DANGER, ALIGN_LEFT, tr("No network connection"));
    }
#if defined(SWANUI_XBOX)
    else if (about_qr)
#else
    else if (tab()->rows == ABOUT)
#endif
    {
        /* About: the GitHub page, for a phone */
        float qs = 240, qx = x + (w - qs) * 0.5f, qy = top + 300;
        draw_project_qr(qx, qy, qs);
        text_draw(x + w * 0.5f, qy + qs + 30, 22, FONT_BOLD, TH_TEXT, ALIGN_CENTER,
                  tr("Scan for the GitHub page"));
        text_draw(x + w * 0.5f, qy + qs + 60, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
                  "Odin: see the credits file in the package");
#if defined(ARES_UWP)
        text_draw(x + w * 0.5f, qy + qs + 88, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
                  tr("Emulator: https://github.com/Tovarichtch/Odin")); /* ARES_UWP: the QR stays the interface's page */
#elif defined(TSUGARU_UWP)
        text_draw(x + w * 0.5f, qy + qs + 88, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER,
                  "Emulator: https://github.com/captainys/TOWNSEMU"); /* TSUGARU_UWP: the QR stays the interface's page */
#else
        text_draw(x + w * 0.5f, qy + qs + 88, 20, FONT_REGULAR, TH_TEXT_DIM, ALIGN_CENTER, "https://x.com/AngelXex1");
#endif
    }
    if (r->kind != K_INFO && r->kind != K_ACTION)
    {
        icon_draw(r->apply == APPLY_NOW ? ICON_CHECK : ICON_CLOCK, x + 32, top + 684, 28, TH_FOCUS);
        text_draw(x + 72, top + 686, 22, FONT_REGULAR, TH_FOCUS, ALIGN_LEFT, tr(notes[r->apply]));
    }
    if (r->global_only && S.game_scope && scope_game())
    {
        icon_draw(ICON_WORLD, x + 32, top + 646, 28, TH_TEXT_DIM);
        text_draw(x + 72, top + 648, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, tr("Same for every game"));
    }
}

#if defined(ARES_UWP)
/* ---------------------------------------------------------------- one system's options (ARES_UWP) */

/* The page Settings > Systems and the game menu's "<System> settings" open: the options host_sysopt_* lists for one
 * system (compat/swanui_sysopts.h), drawn like the settings rows, with the help on the right. Values change in place;
 * the host keeps them in <root>/systems/<id>.cfg and has the running game take the live ones at once. */
#define SYSOPT_PAGE_MAX 64

static struct
{
    char id[16];
    int cursor;
    float sel_y, scroll;
    float switch_t[SYSOPT_PAGE_MAX];
} P;

static SysOpt P_opts[SYSOPT_PAGE_MAX];

void sysopts_page_open(const char *system_id)
{
    memset(&P, 0, sizeof(P));
    str_copy(P.id, sizeof(P.id), system_id ? system_id : "");
    P.sel_y = -1;
}

static bool sysopt_is_switch(const SysOpt *o)
{
    return o->count == 2 && strcmp(o->values[0], "Off") == 0 && strcmp(o->values[1], "On") == 0;
}

/* a value's label in the menus' language; a port's "Automatic (Control Pad)" keeps the device's own name */
static const char *sysopt_label(const SysOpt *o, int v, char *buf, size_t size)
{
    if (v < 0 || v >= o->count)
        return "";
    const char *label = o->values[v];
    if (o->kind == SYSOPT_PORT && strncmp(label, "Automatic (", 11) == 0)
    {
        snprintf(buf, size, "%s (%s", tr("Automatic"), label + 11);
        return buf;
    }
    return tr(label);
}

static void sysopts_draw_help(const SysOpt *o, bool running)
{
    const float w = 476, x = plat_width() - TH_MARGIN - w, top = 214, h = 746;
    draw_rrect(x, top, w, h, TH_RADIUS, TH_CARD);
    if (!o)
        return;
    icon_draw(o->kind == SYSOPT_PORT ? ICON_DEVICE_GAMEPAD_2 : ICON_CPU, x + 32, top + 32, 40, TH_FOCUS);
    text_draw_fit(x + 32, top + 92, 30, FONT_BOLD, TH_TEXT, ALIGN_LEFT, w - 64, tr(o->name));
    float y = top + 150;
    y += draw_wrapped_fit(x + 32, y, w - 64, 24, TH_TEXT_SOFT, tr(o->help), top + 590 - y) + 20;
    char buf[SYSOPT_LABEL + 32], line[SYSOPT_LABEL + 96];
    snprintf(line, sizeof(line), tr("Default: %s"), sysopt_label(o, o->def, buf, sizeof(buf)));
    text_draw_fit(x + 32, top + 636, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, w - 64, line);
    const char *note = o->applies_now               ? "Applies right away"
                       : (running && o->pending)    ? "Restart the game to apply (game menu)"
                                                    : "From the next game you start";
    icon_draw(o->applies_now ? ICON_CHECK : ICON_CLOCK, x + 32, top + 684, 28, TH_FOCUS);
    text_draw_fit(x + 72, top + 686, 22, FONT_REGULAR, TH_FOCUS, ALIGN_LEFT, w - 104, tr(note));
}

bool sysopts_page(uint32_t pressed)
{
    const SwanSystem *sys = swanui_system_by_id(P.id);
    int count = host_sysopt_count(P.id);
    if (count > SYSOPT_PAGE_MAX)
        count = SYSOPT_PAGE_MAX;
    for (int i = 0; i < count; ++i)
        if (!host_sysopt_get(P.id, i, &P_opts[i]))
        {
            count = i;
            break;
        }
    if (P.cursor >= count)
        P.cursor = count > 0 ? count - 1 : 0;
    const bool running = host_sysopt_running(P.id);

    /* ------------------------------------------------ input */
    if (pressed & BIT(BTN_CIRCLE))
    {
        sfx_play(SFX_BACK);
        return false;
    }
    if (count > 0)
    {
        const int step = (pressed & BIT(BTN_DOWN)) ? 1 : (pressed & BIT(BTN_UP)) ? -1 : 0;
        if (step)
        {
            P.cursor = (P.cursor + step + count) % count;
            sfx_play(SFX_CLICK);
        }
        SysOpt *o = &P_opts[P.cursor];
        int delta = (pressed & BIT(BTN_RIGHT)) ? 1 : (pressed & BIT(BTN_LEFT)) ? -1 : 0;
        if (pressed & BIT(BTN_CROSS))
            delta = 1;
        if (delta && o->count > 1)
        {
            const int v = (o->current + delta + o->count) % o->count;
            if (host_sysopt_set(P.id, P.cursor, v))
                host_sysopt_get(P.id, P.cursor, o);
            sfx_play(SFX_CLICK);
        }
        if ((pressed & BIT(BTN_TRIANGLE)) && o->current != o->def)
        {
            if (host_sysopt_set(P.id, P.cursor, o->def))
                host_sysopt_get(P.id, P.cursor, o);
            sfx_play(SFX_CLICK);
        }
    }
    if (pressed & BIT(BTN_SQUARE))
    {
        host_sysopt_reset(P.id);
        for (int i = 0; i < count; ++i)
            host_sysopt_get(P.id, i, &P_opts[i]);
        char msg[160];
        snprintf(msg, sizeof(msg), tr("%s: every option back to its default"), sys ? sys->name : P.id);
        app_toast(msg);
        sfx_play(SFX_SELECT);
    }

    /* ------------------------------------------------ header */
    char title[128];
    snprintf(title, sizeof(title), tr("%s settings"), sys ? sys->name : P.id);
    text_draw_fit(TH_MARGIN, 40, 44, FONT_BOLD, TH_TEXT, ALIGN_LEFT, plat_width() - 2 * TH_MARGIN, title);
    text_draw_fit(TH_MARGIN + 2, 100, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, plat_width() - 2 * TH_MARGIN,
                  tr(running ? "Kept for every game of this system. The game running now takes most changes at once."
                             : "Kept for every game of this system. A few options appear once one of its games has run."));

    /* ------------------------------------------------ rows (as draw_rows) */
    const float x = TH_MARGIN, w = plat_width() - 2 * TH_MARGIN - 476 - 32, top = 164, bottom = 960, row_h = 78,
                cap_h = 50;
    float ys[SYSOPT_PAGE_MAX], y = 0, sel_top = 0;
    for (int i = 0; i < count; ++i)
    {
        if (i == 0 || strcmp(P_opts[i].group, P_opts[i - 1].group) != 0)
            y += (i ? 22 : 0) + cap_h;
        ys[i] = y;
        if (i == P.cursor)
            sel_top = y;
        y += row_h;
    }
    const float content = y + 8, view = bottom - top;
    float want = P.scroll;
    if (count > 0)
    {
        if (sel_top - want < 60)
            want = sel_top - 60;
        if (sel_top + row_h - want > view - 20)
            want = sel_top + row_h - view + 20;
    }
    if (want > content - view)
        want = content - view;
    if (want < 0)
        want = 0;
    anim_approach(&P.scroll, want, app.dt, TH_SNAP);
    if (count == 0)
    {
        draw_rrect(x, top + 50, w, 120, TH_RADIUS, TH_CARD);
        text_draw_fit(x + 30, top + 92, 26, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, w - 60,
                      tr("This system has no options of its own."));
    }
    plat_set_clip((int)x - 8, (int)top - 8, (int)w + 16, (int)(bottom - top) + 16);
    for (int i = 0; i < count; ++i)
    {
        if (i > 0 && strcmp(P_opts[i].group, P_opts[i - 1].group) == 0)
            continue;
        int last = i;
        while (last + 1 < count && strcmp(P_opts[last + 1].group, P_opts[i].group) == 0)
            ++last;
        const float cy = top + ys[i] - P.scroll;
        text_draw(x + 8, cy - cap_h + 12, 22, FONT_BOLD, TH_TEXT_DIM, ALIGN_LEFT, tr(P_opts[i].group));
        draw_rrect(x, cy - 6, w, (ys[last] - ys[i]) + row_h + 12, TH_RADIUS, TH_CARD);
    }
    if (count > 0)
    {
        const float target = top + sel_top - P.scroll;
        if (P.sel_y < 0)
            P.sel_y = target;
        anim_approach(&P.sel_y, target, app.dt, TH_SNAP);
        draw_rrect(x + 6, P.sel_y, w - 12, row_h - 6, TH_RADIUS_SMALL, TH_ROW_SELECTED);
        draw_rrect_outline(x + 6, P.sel_y, w - 12, row_h - 6, TH_RADIUS_SMALL, 3, TH_FOCUS);
    }
    for (int i = 0; i < count; ++i)
    {
        const SysOpt *o = &P_opts[i];
        const float ry = top + ys[i] - P.scroll;
        if (ry > bottom || ry + row_h < top - 60)
            continue;
        const bool sel = i == P.cursor;
        const char *name = tr(o->name);
        text_draw(x + 30, ry + 21, 28, FONT_REGULAR, TH_TEXT, ALIGN_LEFT, name);
        if (!o->applies_now)
        {
            /* applies when a game starts: a clock, lit when a change waits for that */
            const bool waits = running && o->pending;
            const float nx = x + 30 + text_width(28, FONT_REGULAR, name) + 16;
            icon_draw(ICON_CLOCK, nx, ry + 27, 22, waits ? TH_FOCUS : TH_TEXT_DIM);
            text_draw(nx + 30, ry + 27, 20, FONT_REGULAR, waits ? TH_FOCUS : TH_TEXT_DIM, ALIGN_LEFT,
                      tr(waits ? "Restart to apply" : "Next start"));
        }
        const float right = x + w - 28;
        if (sysopt_is_switch(o))
        {
            float *st = &P.switch_t[i];
            anim_approach(st, (float)o->current, app.dt, TH_SNAP);
            draw_switch(right - 40 * 2.1f, ry + 18, 40, *st);
        }
        else
        {
            char buf[SYSOPT_LABEL + 32];
            draw_choice(right, ry + 15, 46, 24, sel ? (theme.light ? TH_BG : TH_SWITCH_OFF) : TH_PILL, TH_TEXT_SOFT,
                        sysopt_label(o, o->current, buf, sizeof(buf)));
        }
    }
    plat_set_clip(0, 0, 0, 0);
    sysopts_draw_help(count > 0 ? &P_opts[P.cursor] : NULL, running);

    static const int glyphs[] = {GLYPH_CROSS, GLYPH_TRIANGLE, GLYPH_SQUARE, GLYPH_CIRCLE};
    static const char *const labels[] = {"Change", "Default", "All defaults", "Back"};
    app_draw_hints(glyphs, labels, 4, NULL);
    return true;
}
#endif

/* ---------------------------------------------------------------- the screen */

static void draw_header(void)
{
    text_draw(TH_MARGIN, 40, 44, FONT_BOLD, TH_TEXT, ALIGN_LEFT, tr(S.remap ? "Button mapping" : "Settings"));
    char sub[200];
    if (scope_game())
        snprintf(sub, sizeof(sub), "%s  \xc2\xb7  %s", shelf_game_title(scope_game()),
                 tr(S.game_scope ? "This game" : "All games"));
    else
        str_copy(sub, sizeof(sub), tr("Applies to every game"));
    text_draw_fit(TH_MARGIN + 2, 100, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_LEFT, 1000, sub);

    if (!scope_game())
        return;
    /* scope switch, top right */
    const char *a = tr("This game"), *b = tr("All games");
    float wa = text_width(22, FONT_REGULAR, a) + 40, wb = text_width(22, FONT_REGULAR, b) + 40;
    float right = plat_width() - TH_MARGIN, hintw = pad_glyph_width(GLYPH_L2, 26) * 2 + 30;
    float xb = right - hintw - wb, xa = xb - wa - 6, y = 46;
    float target = S.game_scope ? xa : xb;
    if (S.scope_x == 0)
        S.scope_x = target;
    anim_approach(&S.scope_x, target, app.dt, TH_SNAP);
    draw_rrect(xa - 6, y - 6, wa + wb + 18, 58, 29, TH_PILL_A(0xc0));
    draw_rrect(S.scope_x, y, S.game_scope ? wa : wb, 46, 23, TH_TEXT);
    text_draw(xa + 20, y + 11, 22, FONT_REGULAR, S.game_scope ? TH_BG : TH_HINT, ALIGN_LEFT, a);
    text_draw(xb + 20, y + 11, 22, FONT_REGULAR, S.game_scope ? TH_HINT : TH_BG, ALIGN_LEFT, b);
    float gx = right - hintw + 16;
    gx += draw_pad_glyph(GLYPH_L2, gx + pad_glyph_width(GLYPH_L2, 26) * 0.5f, y + 23, 26) + 6;
    draw_pad_glyph(GLYPH_R2, gx + pad_glyph_width(GLYPH_R2, 26) * 0.5f, y + 23, 26);
}

/* The tabs in a row across the top, L1 / R1 at its ends: the highlight
 * slides left and right as the shoulder buttons go. */
static void draw_tabs(void)
{
    const float y = 146, h = 58, icon = 28, size = 22, gap = 6;
    float widths[TAB_MAX], total = 0;
    for (int i = 0; i < TAB_COUNT; ++i)
    {
        widths[i] = 28 + icon + 12 + text_width(size, i == S.tab ? FONT_BOLD : FONT_REGULAR, tr(TABS[i].name)) + 28;
        total += widths[i] + (i ? gap : 0);
    }
    float glyph = pad_glyph_width(GLYPH_L1, 28) + 24;
    float avail = plat_width() - 2 * TH_MARGIN - 2 * glyph;
    float squeeze = total > avail ? avail / total : 1.0f; /* long languages */
    /* the room left spread between the tabs, so they fill the bar */
    float spread = total < avail ? gap + (avail - total) / (TAB_COUNT - 1) : gap;
    float x = TH_MARGIN + glyph, target_x = x, target_w = 0;
    for (int i = 0; i < TAB_COUNT; ++i)
    {
        if (i == S.tab)
        {
            target_x = x;
            target_w = widths[i] * squeeze;
        }
        x += widths[i] * squeeze + spread * squeeze;
    }
    if (S.tab_x == 0)
    {
        S.tab_x = target_x;
        S.tab_w = target_w;
    }
    anim_approach(&S.tab_x, target_x, app.dt, TH_SNAP);
    anim_approach(&S.tab_w, target_w, app.dt, TH_SNAP);
    draw_rrect(TH_MARGIN, y - 6, plat_width() - 2 * TH_MARGIN, h + 12, (h + 12) * 0.5f, TH_PILL_A(0x90));
    draw_rrect(S.tab_x, y, S.tab_w, h, h * 0.5f, TH_ROW_SELECTED);
    draw_pad_glyph(GLYPH_L1, TH_MARGIN + glyph * 0.5f + 4, y + h * 0.5f, 28);
    draw_pad_glyph(GLYPH_R1, plat_width() - TH_MARGIN - glyph * 0.5f - 4, y + h * 0.5f, 28);
    x = TH_MARGIN + glyph;
    for (int i = 0; i < TAB_COUNT; ++i)
    {
        bool on = i == S.tab;
        float w = widths[i] * squeeze;
        icon_draw(TABS[i].icon, x + 28 * squeeze, y + (h - icon) * 0.5f, icon, on ? TH_TEXT : TH_HINT);
        text_draw_fit(x + (28 + icon + 12) * squeeze, y + (h - size) * 0.5f - 2, size, on ? FONT_BOLD : FONT_REGULAR,
                      on ? TH_TEXT : TH_HINT, ALIGN_LEFT, w - (28 + icon + 12) * squeeze - 12, tr(TABS[i].name));
        x += widths[i] * squeeze + spread * squeeze;
    }
}

static void draw_rows(void)
{
    const float x = TH_MARGIN, w = plat_width() - 2 * TH_MARGIN - 476 - 32, top = 236, bottom = 960, row_h = 78,
                cap_h = 50;
    const Tab *t = tab();
    /* layout pass: row positions in content space */
    float ys[MAX_ROWS], y = 0, sel_top = 0;
    for (int i = 0; i < t->count && i < MAX_ROWS; ++i)
    {
        if (t->rows[i].group)
            y += (i ? 22 : 0) + cap_h;
        ys[i] = y;
        if (i == S.cursor)
            sel_top = y;
        y += row_h;
    }
    float content = y + 8;
    /* keep the selected row in view */
    float view = bottom - top, want = S.scroll;
    if (S.cursor >= 0)
    {
        if (sel_top - want < 60)
            want = sel_top - 60;
        if (sel_top + row_h - want > view - 20)
            want = sel_top + row_h - view + 20;
    }
    if (want > content - view)
        want = content - view;
    if (want < 0)
        want = 0;
    anim_approach(&S.scroll, want, app.dt, TH_SNAP);

    plat_set_clip((int)x - 8, (int)top - 8, (int)w + 16, (int)(bottom - top) + 16);
    /* cards behind each group */
    for (int i = 0; i < t->count; ++i)
    {
        if (!t->rows[i].group)
            continue;
        int last = i;
        while (last + 1 < t->count && !t->rows[last + 1].group)
            ++last;
        float cy = top + ys[i] - S.scroll;
        text_draw(x + 8, cy - cap_h + 12, 22, FONT_BOLD, TH_TEXT_DIM, ALIGN_LEFT, tr(t->rows[i].group));
        draw_rrect(x, cy - 6, w, (ys[last] - ys[i]) + row_h + 12, TH_RADIUS, TH_CARD);
    }
    /* the highlight slides between rows */
    if (S.cursor >= 0)
    {
        float target = top + sel_top - S.scroll;
        if (S.sel_y < 0)
            S.sel_y = target;
        anim_approach(&S.sel_y, target, app.dt, TH_SNAP);
        draw_rrect(x + 6, S.sel_y, w - 12, row_h - 6, TH_RADIUS_SMALL, TH_ROW_SELECTED);
        draw_rrect_outline(x + 6, S.sel_y, w - 12, row_h - 6, TH_RADIUS_SMALL, 3, TH_FOCUS);
    }
    for (int i = 0; i < t->count; ++i)
    {
        const Row *r = &t->rows[i];
        float ry = top + ys[i] - S.scroll;
        if (ry > bottom || ry + row_h < top - 60)
            continue;
        bool sel = i == S.cursor;
        bool dimmed = r->global_only && S.game_scope && scope_game();
        const bool locked = row_locked(r);
        text_draw(x + 30, ry + 21, 28, FONT_REGULAR, (dimmed && (locked || !sel)) ? TH_TEXT_DIM : TH_TEXT, ALIGN_LEFT,
                  tr(row_name(r)));
        if (dimmed)
            icon_draw(ICON_WORLD, x + 40 + text_width(28, FONT_REGULAR, tr(row_name(r))), ry + 24, 24,
                      TH_TEXT_DIM);
        float right = x + w - 28;
        char buf[SwanStationPS5_PATH_MAX + 64];
        if (locked)
        {
            /* greyed out, with a lock: not editable here */
            const char *label = tr("Global setting.");
            float pw = text_width(22, FONT_REGULAR, label) + 84;
            draw_rrect(right - pw, ry + 15, pw, 46, 23, argb_alpha(TH_PILL, 0.55f));
            icon_draw(ICON_LOCK, right - pw + 18, ry + 25, 26, TH_TEXT_DIM);
            text_draw(right - 20, ry + 25, 22, FONT_REGULAR, TH_TEXT_DIM, ALIGN_RIGHT, label);
            continue;
        }
        switch (r->kind)
        {
        case K_CHOICE:
            draw_choice(right, ry + 15, 46, 24, sel ? (theme.light ? TH_BG : TH_SWITCH_OFF) : TH_PILL,
                        TH_TEXT_SOFT, value_label(r, buf, sizeof(buf)));
            break;
        case K_TOGGLE:
        {
            float *st = &S.switch_t[i < MAX_ROWS ? i : MAX_ROWS - 1];
            anim_approach(st, (float)read_value(r), app.dt, TH_SNAP);
            draw_switch(right - 40 * 2.1f, ry + 18, 40, *st);
            break;
        }
        case K_ACTION:
            icon_draw(ICON_CHEVRON_RIGHT, right - 36, ry + 21, 34, TH_TEXT_SOFT);
            if (r->special == SP_UPDATE || r->special == SP_USBFOLDER || r->special == SP_NFSHARES ||
                r->special == SP_SYSTEM) /* ARES_UWP: how many options, or "Playing now" */
                text_draw_fit(right - 50, ry + 24, 22, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_RIGHT, w - 520,
                              value_label(r, buf, sizeof(buf)));
            break;
        case K_INFO:
            if (r->special == SP_DRIVES)
            {
                draw_choice(right, ry + 15, 46, 24, sel ? (theme.light ? TH_BG : TH_SWITCH_OFF) : TH_PILL,
                            TH_TEXT_SOFT, value_label(r, buf, sizeof(buf)));
                break;
            }
            text_draw_fit(right, ry + 24, 24, FONT_REGULAR, TH_TEXT_SOFT, ALIGN_RIGHT, w - 420,
                          value_label(r, buf, sizeof(buf)));
            break;
        }
    }
    plat_set_clip(0, 0, 0, 0);
}

/* The text typed for the USB folder: without slashes at its ends or spaces around, no ".." */
static void usb_folder_set(const char *text)
{
    char name[sizeof(app.global.usb_folder)];
    str_copy(name, sizeof(name), text);
    size_t start = 0, end = strlen(name);
    while (start < end && (name[start] == '/' || name[start] == ' '))
        ++start;
    while (end > start && (name[end - 1] == '/' || name[end - 1] == ' '))
        --end;
    name[end] = '\0';
    memmove(name, name + start, end - start + 1);
    if (strstr(name, ".."))
        name[0] = '\0';
    str_copy(app.global.usb_folder, sizeof(app.global.usb_folder), name);
    str_copy(app.settings.usb_folder, sizeof(app.settings.usb_folder), name);
    app_save_settings();
    app_rescan();
}

void settings_nfs_shares_set(const char *text)
{
    char list[sizeof(app.global.nfs_shares)];
    str_copy(list, sizeof(list), text);
    size_t start = 0, end = strlen(list);
    while (start < end && (list[start] == ' ' || list[start] == '\t'))
        ++start;
    while (end > start && strchr(" \t\r\n;", list[end - 1]))
        --end;
    list[end] = '\0';
    memmove(list, list + start, end - start + 1);
    for (char *p = list; *p; ++p)
        if (*p == '\n' || *p == '\r')
            *p = ';';
    str_copy(app.global.nfs_shares, sizeof(app.global.nfs_shares), list);
    str_copy(app.settings.nfs_shares, sizeof(app.settings.nfs_shares), list);
    app_save_settings();
    char error[256];
    if (!nfs_set_shares(list, error, sizeof(error)) && error[0])
    {
        char *tab = strchr(error, '\t');
        char message[400];
        if (tab)
        {
            *tab = '\0';
            snprintf(message, sizeof(message), "%.200s: %s", error, tr(tab + 1));
        }
        else
            snprintf(message, sizeof(message), "%s", error);
        app_toast_for(message, 6.0f);
    }
    if (list[0])
        app_nfs_notify();
    app_rescan();
}

static void activate(const Row *r)
{
    switch (r->special)
    {
    case SP_REMAP:
        S.remap = true;
        S.remap_cursor = 0;
        S.remap_y = 0;
        sfx_play(SFX_SELECT);
        return;
    case SP_MEMCARDS:
        sfx_play(SFX_SELECT);
        memcards_open(SCREEN_SETTINGS);
        return;
    case SP_WHO:
        S.who = true;
        S.who_cursor = profiles_current();
        S.who_y = 0;
        sfx_play(SFX_SELECT);
        return;
    case SP_PROFILE:
        sfx_play(SFX_SELECT);
        profile_open(SCREEN_SETTINGS);
        return;
    case SP_PLAYERS:
        S.players = true;
        S.assigned = 0;
        for (int i = 0; i < SwanStationPS5_MAX_PADS; ++i)
            S.pad_prev[i] = app.pads[i].buttons; /* the Cross that opened the page doesn't count */
        sfx_play(SFX_SELECT);
        return;
    case SP_STATS:
        sfx_play(SFX_SELECT);
        library_stats_open(SCREEN_SETTINGS);
        return;
#if defined(ARES_UWP)
    case SP_SYSTEM:
    {
        int n = 0;
        const SwanSystem *list = swanui_systems(&n);
        if (r->base < 0 || r->base >= n)
            return;
        sfx_play(SFX_SELECT);
        sysopts_page_open(list[r->base].id);
        S.sys = true;
        return;
    }
#endif
    case SP_NFSHARES:
        if (app.game)
        {
            app_toast("Quit the game first");
            return;
        }
        sfx_play(SFX_SELECT);
        textinput_open(SCREEN_SETTINGS, "NFS shares",
                       "Type one or more nfs:// addresses, separated by ';'. Example: nfs://192.168.1.85/psx",
                       app.global.nfs_shares[0] ? app.global.nfs_shares : "nfs://192.168.1.85/psx", settings_nfs_shares_set);
        return;
#if defined(SWANUI_XBOX) /* SWANUI_XBOX: the native folder picker; its result arrives in swanui_xbox_folder_poll */
    case SP_USBFOLDER:
        sfx_play(SFX_SELECT);
        plat_xbox_pick_folder_begin();
        return;
#else
    case SP_USBFOLDER:
        sfx_play(SFX_SELECT);
        textinput_open(SCREEN_SETTINGS, "Games folder on drives",
                       "Type the folder name: games are looked for on /mnt/ext0/, /mnt/ext1/ and /mnt/usb0/ to /mnt/usb7/",
                       app.global.usb_folder, usb_folder_set);
        return;
#endif
    case SP_BIOS_START:
        sfx_play(SFX_SELECT);
        app_start_bios();
        return;
    case SP_UPDATE:
        sfx_play(SFX_SELECT);
        if (update_state() == UPDATE_AVAILABLE)
            update_install();
        else
            update_check();
        return;
    case SP_WHITELIST:
    {
        /* one line appended to PS5SX2 Helper's list; nothing else changes */
        static const char *const list = "/data/whitelist.txt";
        FILE *f = fopen(list, "r");
        if (!f)
        {
            app_toast("No /data/whitelist.txt: PS5SX2 Helper isn't installed");
            return;
        }
        char line[64];
        bool present = false, ends_newline = true;
        while (fgets(line, sizeof(line), f))
        {
            size_t n = strlen(line);
            ends_newline = n && line[n - 1] == '\n';
            line[strcspn(line, "\r\n")] = '\0';
            present |= strcmp(line, SwanStationPS5_TITLE_ID) == 0;
        }
        fclose(f);
        if (present)
        {
            app_toast("Odin is already in the whitelist");
            return;
        }
        f = fopen(list, "a");
        bool ok = f && fprintf(f, "%s%s\n", ends_newline ? "" : "\n", SwanStationPS5_TITLE_ID) > 0;
        if (f && fclose(f) != 0)
            ok = false;
        SwanStationPS5_log("whitelist: %s %s", SwanStationPS5_TITLE_ID, ok ? "added to /data/whitelist.txt" : "could not be added");
        app_toast(ok ? "Added: reload PS5SX2 Helper or restart the console" : "Could not write /data/whitelist.txt");
        sfx_play(ok ? SFX_SELECT : SFX_BACK);
        return;
    }
    case SP_RESCAN:
        if (app.game)
        {
            app_toast("Quit the game first");
            return;
        }
        if (app.storage_error[0])
            return;
        if (app.global.nfs_shares[0])
            app_nfs_notify();
        app_rescan();
        char msg[64];
        snprintf(msg, sizeof(msg), tr(app.library.count == 1 ? "Found %d game" : "Found %d games"),
                 app.library.count);
        app_toast(msg);
        sfx_play(SFX_SELECT);
        return;
    default:
        return;
    }
}

static void set_scope(bool game)
{
    if (!scope_game() || game == S.game_scope)
        return;
    if (game && !app.game_has_own)
    {
        /* this game's own settings start as a copy of the console-wide ones */
        app.game_has_own = true;
        app_toast("This game now has its own settings");
    }
    S.game_scope = game;
    sfx_play(SFX_CLICK);
}

bool settings_preview_allowed(void)
{
    return !S.remap && !S.players && !S.who && !S.sys;
}

void settings_screen(uint32_t pressed)
{
    if (app.game)
        app_draw_game(45);
    else
        shelf_backdrop();
    if (app.game)
        draw_rect(0, 0, plat_width(), plat_height(), TH_SCRIM);
#if defined(ARES_UWP)
    if (S.sys)
    {
        /* ARES_UWP: one system's options over the whole screen; Circle comes back to the Systems tab */
        if (!sysopts_page(pressed))
        {
            S.sys = false;
            S.sel_y = -1;
        }
        app_draw_toast();
        return;
    }
#endif
    draw_header();
    if (!S.remap && !S.players && !S.who) /* those pages use the whole screen */
        draw_tabs();

    if (S.remap)
    {
        remap_page(pressed);
        app_draw_toast();
        return;
    }
    if (S.players)
    {
        players_page(pressed);
        app_draw_toast();
        return;
    }
    if (S.who)
    {
        who_page(pressed);
        app_draw_toast();
        return;
    }
    sync_framegen_rows();
    if (S.cursor < 0 || S.cursor >= tab()->count)
        first_selectable();

    /* ------------------------------------------------ input */
    int tab_step = (pressed & BIT(BTN_R1)) ? 1 : (pressed & BIT(BTN_L1)) ? -1 : 0;
    if (tab_step)
    {
        S.tab = (S.tab + tab_step + TAB_COUNT) % TAB_COUNT;
        first_selectable();
        S.scroll = 0;
        S.sel_y = -1;
        sfx_play(SFX_CLICK);
    }
    if (pressed & BIT(BTN_L2))
        set_scope(true);
    if (pressed & BIT(BTN_R2))
        set_scope(false);
    const Tab *t = tab();
    int row_step = (pressed & BIT(BTN_DOWN)) ? 1 : (pressed & BIT(BTN_UP)) ? -1 : 0;
    if (row_step && S.cursor >= 0)
    {
        int c = S.cursor;
        do
            c = (c + row_step + t->count) % t->count;
        while (!selectable(&t->rows[c]) && c != S.cursor);
        if (c != S.cursor)
        {
            S.cursor = c;
            sfx_play(SFX_CLICK);
        }
    }
    const Row *r = S.cursor >= 0 ? &t->rows[S.cursor] : NULL;
    if (r)
    {
        int delta = (pressed & BIT(BTN_RIGHT)) ? 1 : (pressed & BIT(BTN_LEFT)) ? -1 : 0;
        if ((pressed & BIT(BTN_CROSS)) && (r->kind == K_CHOICE || r->kind == K_TOGGLE))
            delta = 1;
        if (delta && (r->kind == K_CHOICE || r->kind == K_TOGGLE))
        {
            int count = r->kind == K_TOGGLE ? 2 : r->count;
            int v = read_value(r);
            for (int k = 0; k < count; ++k)
            {
                v = (v + count + delta) % count;
                if (!value_conflicts(r, v))
                    break;
            }
            write_value(r, v);
            sfx_play(SFX_CLICK); /* also previews a new interface sound */
        }
        if (r->special == SP_DRIVES && (delta || (pressed & BIT(BTN_CROSS))))
        {
            int count = app.drive_count;
            if (count > 1)
            {
                S.drive = (S.drive + count + (delta ? delta : 1)) % count;
                sfx_play(SFX_CLICK);
            }
        }
        if ((pressed & BIT(BTN_CROSS)) && r->kind == K_ACTION)
            activate(r);
        if (pressed & BIT(BTN_TRIANGLE))
        {
            default_value(r);
            sfx_play(SFX_CLICK);
        }
    }
    if ((pressed & BIT(BTN_SQUARE)) && scope_game() && app.game_has_own)
    {
        /* back to the console-wide settings for this game */
        char path[SwanStationPS5_PATH_MAX];
        app_game_config_path(path, sizeof(path), scope_game());
        remove(path);
        app.game_has_own = false;
        app.settings = app.global;
        if (app.game)
        {
            host_apply_settings(&app.settings);
            controls_reconfigure(app.game, &app.settings);
        }
        S.game_scope = false;
        app_toast("This game uses the settings for all games again");
    }
    if (pressed & BIT(BTN_CIRCLE))
    {
        app_save_settings();
        app_close_shelf_settings();
        sfx_play(SFX_BACK);
        app.screen = app.settings_return;
        return;
    }

    draw_rows();
    draw_help(r);
    if (scope_game() && app.game_has_own && S.game_scope)
    {
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_TRIANGLE, GLYPH_SQUARE, GLYPH_TOUCHPAD};
        const char *const labels[] = {"Change", "Back", "Default", "Use all-games settings",
                                      app.settings.manual_crop ? "Preview with manual crop" : "Preview"};
        app_draw_hints(glyphs, labels, app.game ? 5 : 4, NULL);
        const HintCombo right[1] = {{{GLYPH_L1, GLYPH_R1}, 2, '/', "Tabs"}};
        app_draw_hints_right(right, 1);
    }
    else
    {
        static const int glyphs[] = {GLYPH_CROSS, GLYPH_CIRCLE, GLYPH_TRIANGLE, GLYPH_TOUCHPAD};
        const char *const labels[] = {"Change", "Back", "Default",
                                      app.settings.manual_crop ? "Preview with manual crop" : "Preview"};
        app_draw_hints(glyphs, labels, app.game ? 4 : 3, NULL);
        const HintCombo right[1] = {{{GLYPH_L1, GLYPH_R1}, 2, '/', "Tabs"}};
        app_draw_hints_right(right, 1);
    }
    app_draw_toast();
}

/* ---------------------------------------------------------------- the phone page */

static size_t json_text(char *out, size_t size, size_t at, const char *s)
{
    if (at + 2 >= size)
        return at;
    out[at++] = '"';
    for (; *s && at + 8 < size; ++s)
    {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
        {
            out[at++] = '\\';
            out[at++] = (char)c;
        }
        else if (c < 0x20)
            at += (size_t)snprintf(out + at, size - at, "\\u%04x", c);
        else
            out[at++] = (char)c;
    }
    out[at++] = '"';
    out[at] = '\0';
    return at;
}

#define PUT(...) (at += (size_t)snprintf(out + at, at < size ? size - at : 0, __VA_ARGS__))

/* Every tab and setting the phone page shows, with values, in the menus' language. */
int settings_json(char *out, size_t size)
{
    ensure_rows();
    size_t at = 0;
    out[0] = '\0';
    PUT("{\"game\":");
    at = json_text(out, size, at, app.game ? shelf_game_title(app.game) : tr("Applies to every game"));
    PUT(",\"tabs\":[");
    for (int t = 0; t < TAB_COUNT && at < size; ++t)
    {
        PUT("%s{\"name\":", t ? "," : "");
        at = json_text(out, size, at, tr(TABS[t].name));
        PUT(",\"rows\":[");
        bool first = true;
        for (int i = 0; i < TABS[t].count && at < size; ++i)
        {
            const Row *r = &TABS[t].rows[i];
            if (r->kind == K_ACTION || is_swanstation_row(r))
                continue; /* menus, pages and SwanStation's long option lists stay on the console */
            PUT("%s{\"key\":\"%d.%d\",\"name\":", first ? "" : ",", t, i);
            first = false;
            at = json_text(out, size, at, tr(row_name(r)));
            if (r->group)
            {
                PUT(",\"group\":");
                at = json_text(out, size, at, tr(r->group));
            }
            PUT(",\"help\":");
            at = json_text(out, size, at, tr(row_help(r)));
            if (r->kind == K_INFO)
            {
                char buf[SwanStationPS5_PATH_MAX + 64];
                PUT(",\"kind\":\"info\",\"text\":");
                at = json_text(out, size, at, value_label(r, buf, sizeof(buf)));
            }
            else
            {
                PUT(",\"kind\":\"%s\",\"value\":%d,\"labels\":[", r->kind == K_TOGGLE ? "toggle" : "choice",
                    read_value(r));
                int count = r->kind == K_TOGGLE ? 2 : r->count;
                for (int v = 0; v < count && at < size; ++v)
                {
                    PUT("%s", v ? "," : "");
                    at = json_text(out, size, at, r->special == SP_LANGUAGE ? i18n_name(v) : tr(r->labels[v]));
                }
                PUT("]");
            }
            PUT("}");
        }
        PUT("]}");
    }
    /* the RetroAchievements account, for the sign-in card on the Achievements tab */
    int ra_tab = 0;
#if !defined(ODIN_UWP)
    for (int t = 0; t < TAB_COUNT; ++t)
        if (TABS[t].rows == ACHIEVEMENTS)
            ra_tab = t;
#endif
    int nfs_tab = 0;
    for (int t = 0; t < TAB_COUNT; ++t)
        if (TABS[t].rows == SYSTEM)
            nfs_tab = t;
    PUT("],\"nfs\":{\"tab\":%d,\"shares\":", nfs_tab);
    at = json_text(out, size, at, app.global.nfs_shares);
    PUT("},\"ra\":{\"tab\":%d,\"signed_in\":%s,\"user\":", ra_tab, ra_signed_in() ? "true" : "false");
    at = json_text(out, size, at, ra_user());
    PUT(",\"profile\":");
    at = json_text(out, size, at, profiles_name(profiles_current()));
    PUT("}}");
    return (int)(at < size ? at : size - 1);
}

/* A change from the phone: "tab.row" and the new value. Returns the setting's
 * name, or NULL when the key is unknown. */
const char *settings_set_by_key(const char *key, int value)
{
    ensure_rows();
    int t = -1, i = -1;
    if (sscanf(key, "%d.%d", &t, &i) != 2 || t < 0 || t >= TAB_COUNT || i < 0 || i >= TABS[t].count)
        return NULL;
    const Row *r = &TABS[t].rows[i];
    if (r->kind != K_CHOICE && r->kind != K_TOGGLE)
        return NULL;
    int count = r->kind == K_TOGGLE ? 2 : r->count;
    if (value < 0 || value >= count || value_conflicts(r, value))
        return NULL;
    /* the phone edits what the console's settings would: this game's own when it has them */
    bool scope = S.game_scope;
    S.game_scope = app.game && app.game_has_own;
    write_value(r, value);
    S.game_scope = scope;
    return r->name;
}
