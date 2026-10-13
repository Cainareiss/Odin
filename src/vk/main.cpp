/* 
 * Valkyrie
 * Copyright (C) 2011, Stefano Teso
 *
 * Valkyrie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Valkyrie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Valkyrie.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "vk/machine.h"
#include "vk/input.h"
#include "vk/renderer.h"
#include "vk/games.h"

#include <time.h>

#ifdef VK_HAVE_HIKARU
#include "mach/hikaru/hikaru.h"
#endif

unsigned vk_verbosity = 0;

static struct {
	char rom_path[256];
	char rom_name[256];
	int num_frames;
	char state_path[256];
	unsigned save_frames[16];
	unsigned num_save_frames;
	bool unthrottled;
	unsigned scale;
	unsigned anisotropy;
	unsigned window;
	bool fullscreen;
} options;

static vk_game_list_t *game_list;
static vk_game_t *game;
static vk_machine_t *mach;

static int
load_or_save_state (vk_machine_t *mach, bool flag)
{
	char *path;
	int ret;

	ret = asprintf (&path, "%s.vkstate", mach->game->name);
	if (ret <= 0)
		goto fail;

	ret = flag ? vk_machine_load_state (mach, path) :
	             vk_machine_save_state (mach, path);

	if (!ret)
		printf ("%s state '%s'", flag ? "loaded" : "saved", path);
	else
		VK_ERROR ("failed to %s state '%s'", flag ? "load" : "save", path);

fail:
	free (path);
	return ret;
}

static bool paused = false;

static bool
process_events (void)
{
	SDL_Event event;
	bool quit = false;

	while (SDL_PollEvent (&event)) {
		switch (event.type) {
		case SDL_KEYDOWN:
			vk_input_set_key (event.key.keysym.sym, true);
			switch (event.key.keysym.sym) {
			case SDLK_ESCAPE:
				quit = true;
				break;
			case SDLK_SPACE:
				paused ^= 1;
				break;
			case SDLK_F1:
				load_or_save_state (mach, true);
				break;
			case SDLK_F4:
				load_or_save_state (mach, false);
				break;
			case SDLK_F11:
				vk_renderer_toggle_fullscreen (mach->renderer);
				break;
			case SDLK_RETURN:
				if (event.key.keysym.mod & KMOD_ALT)
					vk_renderer_toggle_fullscreen (mach->renderer);
				break;
			default:
				break;
			}
			break;
		case SDL_KEYUP:
			vk_input_set_key (event.key.keysym.sym, false);
			break;
		case SDL_QUIT:
			quit = true;
			break;
		}
	}
	return quit;
}

/* -s: <game>-<frame>.vkstate, written once the frame is drawn */
static void
save_state_at_frame (vk_machine_t *mach)
{
	char path[64];
	unsigned i;

	for (i = 0; i < options.num_save_frames; i++) {
		if (options.save_frames[i] != mach->frame)
			continue;
		snprintf (path, sizeof (path), "%s-%06u.vkstate",
		          mach->game->name, mach->frame);
		if (vk_machine_save_state (mach, path))
			VK_ERROR ("failed to save state '%s'", path);
		else
			VK_PRINT ("saved state '%s'", path);
	}
}

/* Holds the frames at the rate of the machine, 60 per second. A frame late
 * by less than two periods is caught up, a longer delay is dropped. */
static void
limit_frame_rate (void)
{
	static struct timespec next;
	const long period = 1000000000L / 60;
	struct timespec now;
	long long late;

	clock_gettime (CLOCK_MONOTONIC, &now);
	next.tv_nsec += period;
	if (next.tv_nsec >= 1000000000L) {
		next.tv_sec++;
		next.tv_nsec -= 1000000000L;
	}
	late = (now.tv_sec - next.tv_sec) * 1000000000LL +
	       (now.tv_nsec - next.tv_nsec);
	if (late > 2 * period)
		next = now;
	else if (late < 0)
		clock_nanosleep (CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
}

static void
main_loop (vk_machine_t *mach, int num_frames)
{
	while (!process_events ()) {
		if (num_frames > 0 && mach->frame >= (unsigned) num_frames)
			return;
		if (!paused) {
			vk_renderer_begin_frame (mach->renderer);
			vk_machine_run_frame (mach);
			vk_renderer_end_frame (mach->renderer, mach->frame);
			save_state_at_frame (mach);
			if (!options.unthrottled)
				limit_frame_rate ();
		}
	}
}

static const char global_opts[] = "R:r:n:l:s:ux:a:w:fvh?";
static const char global_help[] = "Usage: %s [options]\n"
"	-R <path>	Path to the ROM directory\n"
"	-r <string>	Name of the game to run\n"
"	-n <num>	Stop after the frame num\n"
"	-l <path>	Load a state at startup\n"
"	-s <num>[,...]	Save <game>-<num>.vkstate after these frames\n"
"	-u		Run as fast as the host can, not at 60 frames per second\n"
"	-x <num>	Draw the 3D num times finer than 640 by 480 (1 to 6)\n"
"	-a <num>	Filter the textures with an anisotropy of num (2 to 16); the board has none\n"
"	-w <num>	Make the window num times 640 by 480, as far as the display has room\n"
"	-f		Take the whole display (F11 or Alt+Enter goes back and forth)\n"
"	-v		Set verbosity (affects debug messages)\n"
"	-h              Show this help\n";

static void
show_help (int argc, char **argv)
{
	fprintf (stdout, global_help, argv[0]);
}

static int
parse_global_opts (int argc, char **argv)
{
	int opt;

	memset (&options, 0, sizeof (options));

	if (argc < 2) {
		show_help (argc, argv);
		return -1;
	}

	while ((opt = getopt (argc, argv, global_opts)) != -1) {
		switch (opt) {
		case 'R':
			strncpy (options.rom_path, optarg, 256);
			break;
		case 'r':
			strncpy (options.rom_name, optarg, 32);
			break;
		case 'n':
			options.num_frames = atoi (optarg);
			break;
		case 'l':
			strncpy (options.state_path, optarg, 255);
			break;
		case 's': {
			const char *s = optarg;
			while (s && *s && options.num_save_frames < NUMELEM (options.save_frames)) {
				options.save_frames[options.num_save_frames++] = (unsigned) atoi (s);
				s = strchr (s, ',');
				if (s)
					s++;
			}
			break;
		}
		case 'u':
			options.unthrottled = true;
			break;
		case 'x':
			options.scale = (unsigned) atoi (optarg);
			break;
		case 'a':
			options.anisotropy = (unsigned) atoi (optarg);
			break;
		case 'w':
			options.window = (unsigned) atoi (optarg);
			break;
		case 'f':
			options.fullscreen = true;
			break;
		case 'v':
			vk_verbosity = 1;
			break;
		default:
			VK_ERROR ("unrecognized option '%c'", opt);
			/* fall-through */
		case 'h':
		case '?':
			show_help (argc, argv);
			return -1;
		}
	}
	return 0;
}

static vk_machine_t *
get_machine_for_game (vk_game_t *game, const char *game_name)
{
	char *mach = game->mach;
	if (!strcmp (mach, "hikaru"))
		return hikaru_new (game);
	return NULL;
}

static void
finalize (void)
{
	printf ("Finalizing");
	if (mach)
		vk_machine_destroy (&mach);
}

static char *
get_home_dir (void)
{
	char *path;
	path = getenv ("HOME");
	if (!path) {
		struct passwd *pwd = getpwuid (getuid ());
		if (pwd)
			path = pwd->pw_dir;
	}
	return path;
}

/* The list of the games: in the folder Odin is started from, else in the
 * home of the user, else where `make install` puts it. */
static vk_game_list_t *
load_game_list (void)
{
	const char *home = get_home_dir ();
	char paths[3][512];
	unsigned i;

	snprintf (paths[0], sizeof (paths[0]), "./vk-games.json");
	snprintf (paths[1], sizeof (paths[1]), "%s/vk-games.json", home ? home : ".");
	snprintf (paths[2], sizeof (paths[2]), "%s/.local/share/odin/vk-games.json", home ? home : ".");

	for (i = 0; i < 3; i++) {
		vk_game_list_t *list;

		if (access (paths[i], R_OK))
			continue;
		list = vk_game_list_new (paths[i]);
		if (list)
			printf ("loading game list from '%s'", paths[i]);
		return list;
	}
	VK_ERROR ("no list of the games: '%s', '%s' nor '%s'", paths[0], paths[1], paths[2]);
	return NULL;
}

int
main (const int argc, char **argv)
{
	fprintf (stdout, "Odin, Copyright(C) 2026 Réda Chérif-Touil\n");
	fprintf (stdout, "A hard fork of Valkyrie, Copyright(C) 2011-2013 Stefano Teso\n");
	fprintf (stdout, "Released under the GPL3 License.\n");

	if (parse_global_opts (argc, argv))
		goto fail;

	game_list = load_game_list ();
	if (!game_list) {
		VK_ERROR ("failed to load the game list");
		goto fail;
	}

	game = vk_game_new (game_list, options.rom_path, options.rom_name);
	if (!game) {
		VK_ERROR ("failed to load '%s': can't load game files", options.rom_name);
		goto fail;
	}

	mach = get_machine_for_game (game, options.rom_name);
	if (!mach) {
		VK_ERROR ("failed to load '%s': game name not in game list", options.rom_name);
		goto fail;
	}

	atexit (finalize);

	if (options.scale >= 2 && options.scale <= 6)
		mach->renderer->scale = options.scale;
	if (options.anisotropy >= 2 && options.anisotropy <= 16)
		mach->renderer->anisotropy = options.anisotropy;
	if (options.window >= 2 || options.fullscreen)
		vk_renderer_set_window (mach->renderer, options.window ? options.window : 1,
		                        options.fullscreen);

	vk_machine_reset (mach, VK_RESET_TYPE_HARD);

	if (options.state_path[0]) {
		if (vk_machine_load_state (mach, options.state_path)) {
			VK_ERROR ("failed to load state '%s'", options.state_path);
			return 1;
		}
		VK_PRINT ("loaded state '%s', frame %u", options.state_path, mach->frame);
	}

	printf ("Running");
	main_loop (mach, options.num_frames);

fail:
	return 0;
}

