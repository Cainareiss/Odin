/*
 * The part of SDL3 the machine's input and Vulkan code call (M1), without SDL:
 * the gamepad and key name tables (SDL3's strings, so that the bindings of
 * Odin read the same names), and the Vulkan loader calls of SDL, which report
 * failure (the Xbox path loads the Vulkan ICD itself, with the graphics layer,
 * M3). GPL-3.0-or-later.
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <cstring>

namespace {

/* SDL3's gamepad button names (SDL_GetGamepadStringForButton) */
const char *const BUTTON_NAMES[] = {
	"a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder", "rightshoulder",
	"dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad",
};

const char *const AXIS_NAMES[] = {
	"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger",
};

bool
same(const char *a, const char *b)
{
	return a && b && SDL_strcasecmp(a, b) == 0;
}

} /* namespace */

/* Keys: SDL3's names for the keys the defaults of bindings.cpp use (letters,
 * digits, arrows, the Ctrl, Alt and Shift keys) and the common ones. A key
 * outside these tables has no name (""), as an unknown key has in SDL. */
struct KeyName {
	SDL_Keycode key;
	SDL_Scancode scancode;
	const char *name;
};

const KeyName KEYS[] = {
	{ SDLK_RETURN, SDL_SCANCODE_RETURN, "Return" },   { SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, "Escape" },
	{ SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE, "Backspace" }, { SDLK_TAB, SDL_SCANCODE_TAB, "Tab" },
	{ SDLK_SPACE, SDL_SCANCODE_SPACE, "Space" },     { SDLK_RIGHT, SDL_SCANCODE_RIGHT, "Right" },
	{ SDLK_LEFT, SDL_SCANCODE_LEFT, "Left" },        { SDLK_DOWN, SDL_SCANCODE_DOWN, "Down" },
	{ SDLK_UP, SDL_SCANCODE_UP, "Up" },              { SDLK_LCTRL, SDL_SCANCODE_LCTRL, "Left Ctrl" },
	{ SDLK_RCTRL, SDL_SCANCODE_RCTRL, "Right Ctrl" }, { SDLK_LALT, SDL_SCANCODE_LALT, "Left Alt" },
	{ SDLK_RALT, SDL_SCANCODE_RALT, "Right Alt" },   { SDLK_LSHIFT, SDL_SCANCODE_LSHIFT, "Left Shift" },
	{ SDLK_RSHIFT, SDL_SCANCODE_RSHIFT, "Right Shift" }, { SDLK_INSERT, SDL_SCANCODE_INSERT, "Insert" },
	{ SDLK_DELETE, SDL_SCANCODE_DELETE, "Delete" },  { SDLK_HOME, SDL_SCANCODE_HOME, "Home" },
	{ SDLK_END, SDL_SCANCODE_END, "End" },           { SDLK_PAGEUP, SDL_SCANCODE_PAGEUP, "Page Up" },
	{ SDLK_PAGEDOWN, SDL_SCANCODE_PAGEDOWN, "Page Down" },
	{ SDLK_EQUALS, SDL_SCANCODE_EQUALS, "=" },   { SDLK_MINUS, SDL_SCANCODE_MINUS, "-" },
	{ SDLK_COMMA, SDL_SCANCODE_COMMA, "," },     { SDLK_PERIOD, SDL_SCANCODE_PERIOD, "." },
	{ SDLK_SLASH, SDL_SCANCODE_SLASH, "/" },     { SDLK_SEMICOLON, SDL_SCANCODE_SEMICOLON, ";" },
	{ SDLK_APOSTROPHE, SDL_SCANCODE_APOSTROPHE, "'" }, { SDLK_LEFTBRACKET, SDL_SCANCODE_LEFTBRACKET, "[" },
	{ SDLK_RIGHTBRACKET, SDL_SCANCODE_RIGHTBRACKET, "]" }, { SDLK_BACKSLASH, SDL_SCANCODE_BACKSLASH, "\\" },
	{ SDLK_GRAVE, SDL_SCANCODE_GRAVE, "`" },
	{ SDLK_F1, SDL_SCANCODE_F1, "F1" },   { SDLK_F2, SDL_SCANCODE_F2, "F2" },   { SDLK_F3, SDL_SCANCODE_F3, "F3" },
	{ SDLK_F4, SDL_SCANCODE_F4, "F4" },   { SDLK_F5, SDL_SCANCODE_F5, "F5" },   { SDLK_F6, SDL_SCANCODE_F6, "F6" },
	{ SDLK_F7, SDL_SCANCODE_F7, "F7" },   { SDLK_F8, SDL_SCANCODE_F8, "F8" },   { SDLK_F9, SDL_SCANCODE_F9, "F9" },
	{ SDLK_F10, SDL_SCANCODE_F10, "F10" }, { SDLK_F11, SDL_SCANCODE_F11, "F11" }, { SDLK_F12, SDL_SCANCODE_F12, "F12" },
};

/* The letters and digits: keys 'a'..'z' (named A..Z) and '0'..'9' */
const char *const LETTERS[] = {
	"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
	"N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
};
const char *const DIGITS[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };

/* SDL3's letters and digits by scancode: A is 4 (letters are 4..29), 1..9 are
 * 30..38 and 0 is 39 */
SDL_Keycode
key_of_scancode(SDL_Scancode scancode)
{
	if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
		return static_cast<SDL_Keycode>('a' + (scancode - SDL_SCANCODE_A));
	if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
		return static_cast<SDL_Keycode>('1' + (scancode - SDL_SCANCODE_1));
	if (scancode == SDL_SCANCODE_0)
		return SDLK_0;
	for (const KeyName &entry : KEYS)
		if (entry.scancode == scancode)
			return entry.key;
	return SDLK_UNKNOWN;
}

SDL_Scancode
scancode_of_key(SDL_Keycode key)
{
	if (key >= 'a' && key <= 'z')
		return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (key - 'a'));
	if (key >= '1' && key <= '9')
		return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (key - '1'));
	if (key == SDLK_0)
		return SDL_SCANCODE_0;
	for (const KeyName &entry : KEYS)
		if (entry.key == key)
			return entry.scancode;
	return SDL_SCANCODE_UNKNOWN;
}

extern "C" {

SDL_GamepadButton
SDL_GetGamepadButtonFromString(const char *str)
{
	static const SDL_GamepadButton BUTTONS[] = {
		SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
		SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK,
		SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
		SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
		SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
		SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
		SDL_GAMEPAD_BUTTON_TOUCHPAD,
	};

	for (size_t i = 0; i < sizeof(BUTTON_NAMES) / sizeof(BUTTON_NAMES[0]); i++)
		if (same(str, BUTTON_NAMES[i]))
			return BUTTONS[i];
	return SDL_GAMEPAD_BUTTON_INVALID;
}

const char *
SDL_GetGamepadStringForButton(SDL_GamepadButton button)
{
	const int index = static_cast<int>(button);
	static const SDL_GamepadButton BUTTONS[] = {
		SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
		SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK,
		SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
		SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
		SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
		SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
		SDL_GAMEPAD_BUTTON_TOUCHPAD,
	};

	for (size_t i = 0; i < sizeof(BUTTONS) / sizeof(BUTTONS[0]); i++)
		if (BUTTONS[i] == index)
			return BUTTON_NAMES[i];
	return nullptr;
}

SDL_GamepadAxis
SDL_GetGamepadAxisFromString(const char *str)
{
	static const SDL_GamepadAxis AXES[] = {
		SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
		SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
	};

	for (size_t i = 0; i < sizeof(AXIS_NAMES) / sizeof(AXIS_NAMES[0]); i++)
		if (same(str, AXIS_NAMES[i]))
			return AXES[i];
	return SDL_GAMEPAD_AXIS_INVALID;
}

const char *
SDL_GetGamepadStringForAxis(SDL_GamepadAxis axis)
{
	static const SDL_GamepadAxis AXES[] = {
		SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
		SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
	};

	for (size_t i = 0; i < sizeof(AXES) / sizeof(AXES[0]); i++)
		if (AXES[i] == axis)
			return AXIS_NAMES[i];
	return nullptr;
}

/* The Vulkan loader of SDL is not here: the core's graphics layer loads the
 * ICD itself (M3). Each call reports failure, with SDL_GetError's text. */
bool
SDL_Vulkan_LoadLibrary(const char *path)
{
	(void)path;
	return false;
}

void
SDL_Vulkan_UnloadLibrary(void)
{
}

SDL_FunctionPointer
SDL_Vulkan_GetVkGetInstanceProcAddr(void)
{
	return nullptr;
}

/* Name tables of SDL3 for the keys (SDL_GetKeyName and the inverse) */
const char *
SDL_GetKeyName(SDL_Keycode key)
{
	if (key >= 'a' && key <= 'z')
		return LETTERS[key - 'a'];
	if (key >= '0' && key <= '9')
		return DIGITS[key - '0'];
	for (const KeyName &entry : KEYS)
		if (entry.key == key)
			return entry.name;
	return "";
}

SDL_Keycode
SDL_GetKeyFromName(const char *name)
{
	if (!name || !*name)
		return SDLK_UNKNOWN;
	if (name[1] == '\0' && name[0] >= 'a' && name[0] <= 'z')
		return static_cast<SDL_Keycode>(name[0]);
	if (name[1] == '\0' && name[0] >= 'A' && name[0] <= 'Z')
		return static_cast<SDL_Keycode>(name[0] - 'A' + 'a');
	if (name[1] == '\0' && name[0] >= '0' && name[0] <= '9')
		return static_cast<SDL_Keycode>(name[0]);
	for (const KeyName &entry : KEYS)
		if (SDL_strcasecmp(name, entry.name) == 0)
			return entry.key;
	return SDLK_UNKNOWN;
}

SDL_Keycode
SDL_GetKeyFromScancode(SDL_Scancode scancode, SDL_Keymod modifiers, bool key_event)
{
	(void)modifiers;
	(void)key_event;
	return key_of_scancode(scancode);
}

SDL_Scancode
SDL_GetScancodeFromKey(SDL_Keycode key, SDL_Keymod *modifiers)
{
	if (modifiers)
		*modifiers = SDL_KMOD_NONE;
	return scancode_of_key(key);
}

/* SDL's last error: none is kept here */
const char *
SDL_GetError(void)
{
	return "";
}

int
SDL_strcasecmp(const char *a, const char *b)
{
	for (;; a++, b++) {
		const int ca = (*a >= 'A' && *a <= 'Z') ? *a - 'A' + 'a' : *a;
		const int cb = (*b >= 'A' && *b <= 'Z') ? *b - 'A' + 'a' : *b;

		if (ca != cb)
			return ca - cb;
		if (ca == 0)
			return 0;
	}
}

bool
SDL_Vulkan_CreateSurface(SDL_Window *window, VkInstance instance, const VkAllocationCallbacks *allocator,
                         VkSurfaceKHR *surface)
{
	(void)window;
	(void)instance;
	(void)allocator;
	(void)surface;
	return false;
}

const char *const *
SDL_Vulkan_GetInstanceExtensions(Uint32 *count)
{
	if (count)
		*count = 0;
	return nullptr;
}

} /* extern "C" */
