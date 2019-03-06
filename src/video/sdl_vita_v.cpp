/* $Id: sdl_v.cpp 27775 2017-03-11 13:05:54Z frosch $ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file sdl_v.cpp Implementation of the SDL video driver. */

#if defined(WITH_SDL) && defined(__vita__)
#include "vita.h"

#include "../stdafx.h"
#include "../openttd.h"
#include "../gfx_func.h"
#include "../sdl.h"
#include "../rev.h"
#include "../blitter/factory.hpp"
#include "../network/network.h"
#include "../thread/thread.h"
#include "../progress.h"
#include "../core/random_func.hpp"
#include "../core/math_func.hpp"
#include "../fileio_func.h"
#include "sdl_v.h"
#include <SDL.h>

#include "../safeguards.h"

#include "vita2d.h"
#include "vita_touch.h"

int _last_mouse_x = 0;
int _last_mouse_y = 0;

static int _hires_dx = 0; // sub-pixel-precision counters to allow slow pointer motion of <1 pixel per frame
static int _hires_dy = 0;
static int _pressed_right_stick_dirs[4] = { 0, 0, 0, 0 };
static int _pressed_cursor_keys[4] = { 0, 0, 0, 0 };

static void RescaleAnalog(int *x, int *y, int dead);
static void CreateAndPushSdlKeyEvent(uint32_t event_type, SDL_Keycode key);

// these three internal structures from SDL2 are needed to gain access to the raw
// vita2d_texture pointer to set it to ignore alpha values
typedef struct SDL_SW_YUVTexture
{
	Uint32 format;
	Uint32 target_format;
	int w, h;
	Uint8 *pixels;
	int *colortab;
	Uint32 *rgb_2_pix;
	void (*Display1X) (int *colortab, Uint32 * rgb_2_pix,
	unsigned char *lum, unsigned char *cr,
	unsigned char *cb, unsigned char *out,
	int rows, int cols, int mod);
	void (*Display2X) (int *colortab, Uint32 * rgb_2_pix,
	unsigned char *lum, unsigned char *cr,
	unsigned char *cb, unsigned char *out,
	int rows, int cols, int mod);

	/* These are just so we don't have to allocate them separately */
	Uint16 pitches[3];
	Uint8 *planes[3];

	/* This is a temporary surface in case we have to stretch copy */
	SDL_Surface *stretch;
	SDL_Surface *display;
} SDL_SW_YUVTexture;

/* Define the SDL texture structure */
typedef struct SDL_Texture
{
	const void *magic;
	Uint32 format;              /**< The pixel format of the texture */
	int access;                 /**< SDL_TextureAccess */
	int w;                      /**< The width of the texture */
	int h;                      /**< The height of the texture */
	int modMode;                /**< The texture modulation mode */
	SDL_BlendMode blendMode;    /**< The texture blend mode */
	Uint8 r, g, b, a;           /**< Texture modulation values */

	SDL_Renderer *renderer;

	/* Support for formats not supported directly by the renderer */
	SDL_Texture *native;
	SDL_SW_YUVTexture *yuv;
	void *pixels;
	int pitch;
	SDL_Rect locked_rect;

	void *driverdata;           /**< Driver specific texture representation */

	SDL_Texture *prev;
	SDL_Texture *next;
} SDL_Texture;

typedef struct VITA_TextureData
{
	vita2d_texture	*tex;
	unsigned int	pitch;
	unsigned int	w;
	unsigned int	h;
} VITA_TextureData;

static FVideoDriver_SDL iFVideoDriver_SDL;

static SDL_Texture *_sdl_screentext;
// Operations:
// We create a surface to draw to, handle palettes etc
// We draw these to the texture
// Once done, we sync the texture to the screen
static SDL_Surface *_sdl_realsurface;
static SDL_Window *_sdl_window;
static SDL_Renderer *_sdl_renderer;

// The renderer dest format for the conversion
static SDL_PixelFormat *_dst_format;
static SDL_Joystick *_sdl_joystick;

/** Whether the drawing is/may be done in a separate thread. */
static bool _draw_threaded;
/** Thread used to 'draw' to the screen, i.e. push data to the screen. */
static ThreadObject *_draw_thread = NULL;
/** Mutex to keep the access to the shared memory controlled. */
static ThreadMutex *_draw_mutex = NULL;
/** Should we keep continue drawing? */
static volatile bool _draw_continue;
static Palette _local_palette;

#define MAX_DIRTY_RECTS 100
static SDL_Rect _dirty_rects[MAX_DIRTY_RECTS];
static int _num_dirty_rects;
static int _use_hwpalette;
static int _requested_hwpalette; /* Did we request a HWPALETTE for the current video mode? */

#define VITA_JOY_TRIANGLE 0
#define VITA_JOY_CIRCLE   1
#define VITA_JOY_CROSS    2
#define VITA_JOY_SQUARE   3

#define VITA_JOY_LTRIGGER 4
#define VITA_JOY_RTRIGGER 5

// down = 6, left = 7, up = 8, right = 9
#define VITA_JOY_DOWN     6
#define VITA_JOY_LEFT     7
#define VITA_JOY_UP       8
#define VITA_JOY_RIGHT    9

#define VITA_JOY_SELECT  10
#define VITA_JOY_START   11

#define OTTD_DIR_LEFT     1
#define OTTD_DIR_UP       2
#define OTTD_DIR_RIGHT    4
#define OTTD_DIR_DOWN     8

// Scale whatever we have to the vita screen resolution on GPU
SDL_Rect _sdldest {0, 0, 960, 544};

static int _cursor_move_x = 0, _cursor_move_y = 0;

void VideoDriver_SDL::MakeDirty(int left, int top, int width, int height)
{
	if (_num_dirty_rects < MAX_DIRTY_RECTS) {
		_dirty_rects[_num_dirty_rects].x = left;
		_dirty_rects[_num_dirty_rects].y = top;
		_dirty_rects[_num_dirty_rects].w = width;
		_dirty_rects[_num_dirty_rects].h = height;
	}
	_num_dirty_rects++;
}

static void UpdatePalette(bool init = false)
{
	SDL_Color pal[256];

	for (int i = 0; i != _local_palette.count_dirty; i++) {
		pal[i].r = _local_palette.palette[_local_palette.first_dirty + i].r;
		pal[i].g = _local_palette.palette[_local_palette.first_dirty + i].g;
		pal[i].b = _local_palette.palette[_local_palette.first_dirty + i].b;
		pal[i].a = 0;
	}

	SDL_SetPaletteColors(_sdl_realsurface->format->palette, pal, _local_palette.first_dirty, _local_palette.count_dirty);
}

static void DrawSurfaceToScreen();

static void InitPalette()
{
	_local_palette = _cur_palette;
	_local_palette.first_dirty = 0;
	_local_palette.count_dirty = 256;
	UpdatePalette(true);
	DrawSurfaceToScreen();
}

static void CheckPaletteAnim()
{
	if (_cur_palette.count_dirty != 0) {
		Blitter *blitter = BlitterFactory::GetCurrentBlitter();

		switch (blitter->UsePaletteAnimation()) {
			case Blitter::PALETTE_ANIMATION_VIDEO_BACKEND:
				UpdatePalette();
				break;

			case Blitter::PALETTE_ANIMATION_BLITTER:
				blitter->PaletteAnimate(_local_palette);
				break;

			case Blitter::PALETTE_ANIMATION_NONE:
				break;

			default:
				NOT_REACHED();
		}
		_cur_palette.count_dirty = 0;
	}
}

static void DrawSurfaceToScreen()
{
	int n = _num_dirty_rects;
	if (n == 0) return;

	_num_dirty_rects = 0;

	SDL_Surface *temp = SDL_ConvertSurface(_sdl_realsurface, _dst_format, 0);
	SDL_UpdateTexture(_sdl_screentext, NULL, temp->pixels, temp->pitch);
	// We need to scale whatever resolution we've got to the vita resolution
	SDL_RenderCopy(_sdl_renderer, _sdl_screentext, NULL, &_sdldest);
	SDL_RenderPresent(_sdl_renderer);

	SDL_FreeSurface(temp);
}

static void DrawSurfaceToScreenThread(void *)
{
	/* First tell the main thread we're started */
	_draw_mutex->BeginCritical();
	_draw_mutex->SendSignal();

	/* Now wait for the first thing to draw! */
	_draw_mutex->WaitForSignal();

	while (_draw_continue) {
		CheckPaletteAnim();
		/* Then just draw and wait till we stop */
		DrawSurfaceToScreen();
		_draw_mutex->WaitForSignal();
	}

	_draw_mutex->EndCritical();
	_draw_thread->Exit();
}

static const Dimension _default_resolutions[] = {
	{ 960,	544}
};

static void GetVideoModes()
{
	memcpy(_resolutions, _default_resolutions, sizeof(_default_resolutions));
	_num_resolutions = 1;
}

static void HandleAnalogSticks(void)
{
	int left_x = SDL_JoystickGetAxis(_sdl_joystick, 0);
	int left_y = SDL_JoystickGetAxis(_sdl_joystick, 1);
	RescaleAnalog(&left_x, &left_y, 2000);
	_hires_dx += left_x; // sub-pixel precision to allow slow mouse motion at speeds < 1 pixel/frame
	_hires_dy += left_y;
	
	const int slowdown = 6144;
	
	if (_hires_dx != 0 || _hires_dy != 0) {
		int xrel = _hires_dx / slowdown;
		int yrel = _hires_dy / slowdown;
		_hires_dx %= slowdown;
		_hires_dy %= slowdown;
		if (xrel != 0 || yrel != 0) {
			// limit joystick mouse to screen coords, same as physical mouse
			int x = _last_mouse_x + xrel;
			int y = _last_mouse_y + yrel;
			if (x < 0) {
				x = 0;
				xrel = 0 - _last_mouse_x;
			}
			if (x > VITA_DISPLAY_WIDTH) {
				x = VITA_DISPLAY_WIDTH;
				xrel = VITA_DISPLAY_WIDTH - _last_mouse_x;
			}
			if (y < 0) {
				y = 0;
				yrel = 0 - _last_mouse_y;
			}
			if (y > VITA_DISPLAY_HEIGHT) {
				y = VITA_DISPLAY_HEIGHT;
				yrel = VITA_DISPLAY_HEIGHT - _last_mouse_y;
			}
			SDL_Event event;
			event.type = SDL_MOUSEMOTION;
			event.motion.x = x;
			event.motion.y = y;
			event.motion.xrel = xrel;
			event.motion.yrel = yrel;
			SDL_PushEvent(&event);
		}
	}

	// map right stick to map scrolling
	float right_x = SDL_JoystickGetAxis(_sdl_joystick, 2);
	float right_y = -1 * SDL_JoystickGetAxis(_sdl_joystick, 3);
	float right_joy_dead_zone_squared = 10240.0*10240.0;
	float slope = 0.414214f; // tangent of 22.5 degrees for size of angular zones

	int up = 0;
	int down = 0;
	int left = 0;
	int right = 0;

	if ((right_x * right_x + right_y * right_y) > right_joy_dead_zone_squared) {

		// upper right quadrant
		if (right_y > 0 && right_x > 0)
		{
			if (right_y > slope * right_x)
				up = 1;
			if (right_x > slope * right_y)
				right = 1;
		}
		// upper left quadrant
		else if (right_y > 0 && right_x <= 0)
		{
			if (right_y > slope * (-right_x))
				up = 1;
			if ((-right_x) > slope * right_y)
				left = 1;
		}
		// lower right quadrant
		else if (right_y <= 0 && right_x > 0)
		{
			if ((-right_y) > slope * right_x)
				down = 1;
			if (right_x > slope * (-right_y))
				right = 1;
		}
		// lower left quadrant
		else if (right_y <= 0 && right_x <= 0)
		{
			if ((-right_y) > slope * (-right_x))
				down = 1;
			if ((-right_x) > slope * (-right_y))
				left = 1;
		}

		_dirkeys |= ((!_pressed_right_stick_dirs[0] && down) ? OTTD_DIR_DOWN : 0) |
					((!_pressed_right_stick_dirs[1] && left) ? OTTD_DIR_LEFT : 0) |
					((!_pressed_right_stick_dirs[2] && up) ? OTTD_DIR_UP : 0) |
					((!_pressed_right_stick_dirs[3] && right) ? OTTD_DIR_RIGHT : 0);

	}

	uint8 tmp = ((_pressed_right_stick_dirs[0] && !down) ? OTTD_DIR_DOWN : 0) |
				((_pressed_right_stick_dirs[1] && !left) ? OTTD_DIR_LEFT : 0) |
				((_pressed_right_stick_dirs[2] && !up) ? OTTD_DIR_UP : 0) |
				((_pressed_right_stick_dirs[3] && !right) ? OTTD_DIR_RIGHT : 0);
	_dirkeys = _dirkeys &~tmp;

	_pressed_right_stick_dirs[0] = down;
	_pressed_right_stick_dirs[1] = left;
	_pressed_right_stick_dirs[2] = up;
	_pressed_right_stick_dirs[3] = right;
}

static void RescaleAnalog(int *x, int *y, int dead)
{
	//radial and scaled dead_zone
	//http://www.third-helix.com/2013/04/12/doing-thumbstick-dead-zones-right.html
	//input and output values go from -32767...+32767;

	//the maximum is adjusted to account for SCE_CTRL_MODE_DIGITALANALOG_WIDE
	//where a reported maximum axis value corresponds to 80% of the full range
	//of motion of the analog stick

	if (dead == 0) return;
	if (dead >= 32767) {
		*x = 0;
		*y = 0;
		return;
	}

	const float max_axis = 32767.0f;
	float analog_x = (float) *x;
	float analog_y = (float) *y;
	float dead_zone = (float) dead;

	float magnitude = sqrtf(analog_x * analog_x + analog_y * analog_y);
	if (magnitude >= dead_zone) {
		//adjust maximum magnitude
		float abs_analog_x = fabs(analog_x);
		float abs_analog_y = fabs(analog_y);
		float max_x;
		float max_y;
		if (abs_analog_x > abs_analog_y) {
			max_x = max_axis;
			max_y = (max_axis * analog_y) / abs_analog_x;
		} else {
			max_x = (max_axis * analog_x) / abs_analog_y;
			max_y = max_axis;
		}
		float maximum = sqrtf(max_x * max_x + max_y * max_y);
		if (maximum > 1.25f * max_axis) maximum = 1.25f * max_axis;
		if (maximum < magnitude) maximum = magnitude;

		// find scaled axis values with magnitudes between zero and maximum
		float scalingFactor = maximum / magnitude * (magnitude - dead_zone) / (maximum - dead_zone);
		analog_x = (analog_x * scalingFactor);
		analog_y = (analog_y * scalingFactor);

		// clamp to ensure results will never exceed the max_axis value
		float clamping_factor = 1.0f;
		abs_analog_x = fabs(analog_x);
		abs_analog_y = fabs(analog_y);
		if (abs_analog_x > max_axis || abs_analog_y > max_axis) {
			if (abs_analog_x > abs_analog_y)
				clamping_factor = max_axis / abs_analog_x;
			else
				clamping_factor = max_axis / abs_analog_y;
		}

		*x = (int) (clamping_factor * analog_x);
		*y = (int) (clamping_factor * analog_y);
	} else {
		*x = 0;
		*y = 0;
	}
}

static void CreateAndPushSdlKeyEvent(uint32_t event_type, SDL_Keycode key) 
{
	SDL_Event event;
	event.type = event_type;
	event.key.keysym.sym = key;
	event.key.keysym.mod = 0;
	SDL_PushEvent(&event);
}

bool VideoDriver_SDL::CreateMainSurface(uint w, uint h)
{
	// TODO: Implement destroying and recreating this so we can change resolution
	// on the go
	if (_sdl_window != NULL)
	{
		//SDL_DestroyTexture(_sdl_screentext);
		//SDL_DestroyRenderer(_sdl_renderer);
		//SDL_DestroyWindow(_sdl_window);
	}

	char caption[32];
	seprintf(caption, lastof(caption), "OpenTTD %s", _openttd_revision);

	_sdl_window = SDL_CreateWindow(caption, 0, 0, w, h, 0);
	_sdl_renderer = SDL_CreateRenderer(_sdl_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

	_sdl_screentext = SDL_CreateTexture(_sdl_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
	// On Vita, make sure alpha channel is ignored when rendering this texture
	// this fixes the black screen problem on Vita in an efficient way.
	VITA_TextureData *vita_texture = (VITA_TextureData *) _sdl_screentext->driverdata;
	SceGxmTextureFilter min_filter = vita2d_texture_get_min_filter(vita_texture->tex);
	SceGxmTextureFilter mag_filter = vita2d_texture_get_mag_filter(vita_texture->tex);

	vita2d_free_texture(vita_texture->tex);
	vita_texture->tex = vita2d_create_empty_texture_format(w, h, SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1BGR);
	vita2d_texture_set_filters(vita_texture->tex, min_filter, mag_filter);

	_sdl_realsurface = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);

	// Assign _screen values so the rest of openttd can draw to this surface
	_screen.width = w;
	_screen.height = h;
	_screen.pitch = _cur_resolution.width;
	_screen.dst_ptr = _sdl_realsurface->pixels;

	Uint32 format = SDL_PIXELFORMAT_ABGR8888;

	_dst_format = SDL_AllocFormat(format);

	SDL_InitSubSystem(SDL_INIT_JOYSTICK);
	// Let this fail silently
	if (SDL_NumJoysticks() > 0) {
		_sdl_joystick = SDL_JoystickOpen(0);
	}

	/* When in full screen, we will always have the mouse cursor
	 * within the window, even though SDL does not give us the
	 * appropriate event to know this. */
	if (_fullscreen)
		_cursor.in_window = true;

	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	blitter->PostResize();

	InitPalette();

	GameSizeChanged();

	return true;
}

bool VideoDriver_SDL::ClaimMousePointer()
{
	SDL_CALL SDL_ShowCursor(0);
	return true;
}

struct VkMapping {
#if SDL_VERSION_ATLEAST(1, 3, 0)
	SDL_Keycode vk_from;
#else
	uint16 vk_from;
#endif
	byte vk_count;
	byte map_to;
};

#define AS(x, z) {x, 0, z}
#define AM(x, y, z, w) {x, (byte)(y - x), z}

static const VkMapping _vk_mapping[] = {
	/* Pageup stuff + up/down */
	AM(SDLK_PAGEUP, SDLK_PAGEDOWN, WKC_PAGEUP, WKC_PAGEDOWN),
	AS(SDLK_UP,     WKC_UP),
	AS(SDLK_DOWN,   WKC_DOWN),
	AS(SDLK_LEFT,   WKC_LEFT),
	AS(SDLK_RIGHT,  WKC_RIGHT),

	AS(SDLK_HOME,   WKC_HOME),
	AS(SDLK_END,    WKC_END),

	AS(SDLK_INSERT, WKC_INSERT),
	AS(SDLK_DELETE, WKC_DELETE),

	/* Map letters & digits */
	AM(SDLK_a, SDLK_z, 'A', 'Z'),
	AM(SDLK_0, SDLK_9, '0', '9'),

	AS(SDLK_ESCAPE,    WKC_ESC),
	AS(SDLK_PAUSE,     WKC_PAUSE),
	AS(SDLK_BACKSPACE, WKC_BACKSPACE),

	AS(SDLK_SPACE,     WKC_SPACE),
	AS(SDLK_RETURN,    WKC_RETURN),
	AS(SDLK_TAB,       WKC_TAB),

	/* Function keys */
	AM(SDLK_F1, SDLK_F12, WKC_F1, WKC_F12),

	/* Numeric part. */
	AM(SDLK_KP_0, SDLK_KP_9, '0', '9'),
	AS(SDLK_KP_DIVIDE,   WKC_NUM_DIV),
	AS(SDLK_KP_MULTIPLY, WKC_NUM_MUL),
	AS(SDLK_KP_MINUS,    WKC_NUM_MINUS),
	AS(SDLK_KP_PLUS,     WKC_NUM_PLUS),
	AS(SDLK_KP_ENTER,    WKC_NUM_ENTER),
	AS(SDLK_KP_PERIOD,   WKC_NUM_DECIMAL),

	/* Other non-letter keys */
	AS(SDLK_SLASH,        WKC_SLASH),
	AS(SDLK_SEMICOLON,    WKC_SEMICOLON),
	AS(SDLK_EQUALS,       WKC_EQUALS),
	AS(SDLK_LEFTBRACKET,  WKC_L_BRACKET),
	AS(SDLK_BACKSLASH,    WKC_BACKSLASH),
	AS(SDLK_RIGHTBRACKET, WKC_R_BRACKET),

	AS(SDLK_QUOTE,   WKC_SINGLEQUOTE),
	AS(SDLK_COMMA,   WKC_COMMA),
	AS(SDLK_MINUS,   WKC_MINUS),
	AS(SDLK_PERIOD,  WKC_PERIOD)
};

static uint ConvertSdlKeyIntoMy(SDL_Keysym *sym, WChar *character)
{
	const VkMapping *map;
	uint key = 0;

	for (map = _vk_mapping; map != endof(_vk_mapping); ++map) {
		if ((uint)(sym->sym - map->vk_from) <= map->vk_count) {
			key = sym->sym - map->vk_from + map->map_to;
			break;
		}
	}

	/* check scancode for BACKQUOTE key, because we want the key left of "1", not anything else (on non-US keyboards) */
#if defined(WIN32) || defined(__OS2__)
	if (sym->scancode == 41) key = WKC_BACKQUOTE;
#elif defined(__APPLE__)
	if (sym->scancode == 10) key = WKC_BACKQUOTE;
#elif defined(__MORPHOS__)
	if (sym->scancode == 0)  key = WKC_BACKQUOTE;  // yes, that key is code '0' under MorphOS :)
#elif defined(__BEOS__)
	if (sym->scancode == 17) key = WKC_BACKQUOTE;
#elif defined(__SVR4) && defined(__sun)
	if (sym->scancode == 60) key = WKC_BACKQUOTE;
	if (sym->scancode == 49) key = WKC_BACKSPACE;
#elif defined(__sgi__)
	if (sym->scancode == 22) key = WKC_BACKQUOTE;
#else
	if (sym->scancode == 49) key = WKC_BACKQUOTE;
#endif

	if (sym->mod & KMOD_SHIFT) key |= WKC_SHIFT;
	if (sym->mod & KMOD_CTRL)  key |= WKC_CTRL;
	if (sym->mod & KMOD_ALT)   key |= WKC_ALT;

	//*character = sym->unicode;
	// SDL2 removed unicode so we'll just hack this in for ascii support for now
	*character = (char)sym->scancode;
	return key;
}

int VideoDriver_SDL::PollEvent()
{
	SDL_Event ev;

	if (!SDL_CALL SDL_PollEvent(&ev)) return -2;

	bool cursor_updated = false;

	//sceClibPrintf("ev.type: %d", ev.type);

	HandleTouch(&ev);

	switch (ev.type) {
		case SDL_JOYAXISMOTION:
			// Don't handle this event, instead we poll on the draw loop for the most up-to-date joystick
			// state, and generate corresponding mouse events there
			break;
		case SDL_MOUSEMOTION:
			if (_cursor.UpdateCursorPosition(ev.motion.x, ev.motion.y, true)) {
				SDL_CALL SDL_WarpMouseInWindow(_sdl_window, _cursor.pos.x, _cursor.pos.y);
			}
			// update joystick / touch mouse coords
			_last_mouse_x = ev.motion.x;
			_last_mouse_y = ev.motion.y;
			HandleMouseEvents();
			break;

		case SDL_MOUSEWHEEL:
			if(ev.wheel.y < 0) {
				// Zoom out
				_cursor.wheel += 1;
			} else if (ev.wheel.y > 0) {
				// Zoom in
				_cursor.wheel -= 1;
			}			
			HandleMouseEvents();
			break;


		case SDL_MOUSEBUTTONDOWN:
			if (_rightclick_emulate && SDL_CALL SDL_GetModState() & KMOD_CTRL) {
				ev.button.button = SDL_BUTTON_RIGHT;
			}

			switch (ev.button.button) {
				case SDL_BUTTON_LEFT:
					_left_button_down = true;
					break;

				case SDL_BUTTON_RIGHT:
					_right_button_down = true;
					_right_button_clicked = true;
					break;

				default: break;
			}
			HandleMouseEvents();
			break;

		case SDL_MOUSEBUTTONUP:
			if (_rightclick_emulate) {
				_right_button_down = false;
				_left_button_down = false;
				_left_button_clicked = false;
			} else if (ev.button.button == SDL_BUTTON_LEFT) {
				_left_button_down = false;
				_left_button_clicked = false;
			} else if (ev.button.button == SDL_BUTTON_RIGHT) {
				_right_button_down = false;
			}
			HandleMouseEvents();
			break;

		case SDL_WINDOWEVENT:
			if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
				// mouse entered the window, enable cursor
				_cursor.in_window = true;
			}
			break;
		case SDL_JOYBUTTONDOWN:
			// Circle or L for right click
			if (ev.jbutton.button == VITA_JOY_CIRCLE || ev.jbutton.button == VITA_JOY_LTRIGGER)
			{
				_right_button_down = true;
				_right_button_clicked = true;
				HandleMouseEvents();
			}
			// X or R for left click
			else if (ev.jbutton.button == VITA_JOY_CROSS || ev.jbutton.button == VITA_JOY_RTRIGGER)
			{
				_left_button_down = true;
				HandleMouseEvents();
			}
			// Just pretend we're wheeling the mouse wheel
			// than implementing this properly
			else if (ev.jbutton.button == VITA_JOY_SQUARE)
			{
				// Zoom out
				_cursor.wheel += 1;
				HandleMouseEvents();
			}
			else if (ev.jbutton.button == VITA_JOY_TRIANGLE)
			{
				// Zoom in
				_cursor.wheel -= 1;
				HandleMouseEvents();

			}
			// Map d-pad to arrow keys in order to pan screen
			else
			{
				_dirkeys |= (ev.jbutton.button == VITA_JOY_DOWN ? OTTD_DIR_DOWN : 0) |
							(ev.jbutton.button == VITA_JOY_LEFT ? OTTD_DIR_LEFT : 0) |
							(ev.jbutton.button == VITA_JOY_UP ? OTTD_DIR_UP : 0) |
							(ev.jbutton.button == VITA_JOY_RIGHT ? OTTD_DIR_RIGHT : 0);
			}
			break;
		case SDL_JOYBUTTONUP:
			if (ev.jbutton.button == VITA_JOY_CIRCLE || ev.jbutton.button == VITA_JOY_LTRIGGER)
			{
				_right_button_down = false;
				_right_button_clicked = false;
				HandleMouseEvents();
			}
			else if (ev.jbutton.button == VITA_JOY_CROSS || ev.jbutton.button == VITA_JOY_RTRIGGER)
			{
				_left_button_down = false;
				_left_button_clicked = false;
				HandleMouseEvents();
			}
			else
			{
				uint8 tmp = (ev.jbutton.button == VITA_JOY_DOWN ? OTTD_DIR_DOWN : 0) |
							(ev.jbutton.button == VITA_JOY_LEFT ? OTTD_DIR_LEFT : 0) |
							(ev.jbutton.button == VITA_JOY_UP ? OTTD_DIR_UP : 0) |
							(ev.jbutton.button == VITA_JOY_RIGHT ? OTTD_DIR_RIGHT : 0);
				_dirkeys = _dirkeys &~tmp;
			}
			break;
		case SDL_KEYDOWN: // Toggle full-screen on ALT + ENTER/F
			if ((ev.key.keysym.mod & KMOD_ALT) &&
					(ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_f)) {
				ToggleFullScreen(!_fullscreen);
			} else {
				WChar character;
				uint keycode = ConvertSdlKeyIntoMy(&ev.key.keysym, &character);
				HandleKeypress(keycode, character);
			}

			DEBUG(misc, 1, "ev.key.keysym.sym: %d\n", ev.key.keysym.sym);
			break;
	}

	return -1;
}

const char *VideoDriver_SDL::Start(const char * const *parm)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0)
		return "SDL_INIT_VIDEO failed";

	GetVideoModes();

	if (!CreateMainSurface(_cur_resolution.width, _cur_resolution.height)) {
		DEBUG(driver, 1, "CreateMainSurface: %d, %d\n", _cur_resolution.width, _cur_resolution.height);
		return "CreateMainSurface failed";
	}

	int renderCt = SDL_GetNumRenderDrivers();

	if (renderCt < 0) {
		return "SDL_GetNumRenderDrivers: No drivers found";
	}

	SDL_RendererInfo info;
	if (SDL_GetRenderDriverInfo(0, &info))
	{
		DEBUG(driver, 1, "SDL: using driver '%s'", info.name);
	}

	MarkWholeScreenDirty();
	SetupKeyboard();

	_draw_threaded = GetDriverParam(parm, "no_threads") == NULL && GetDriverParam(parm, "no_thread") == NULL;

	return NULL;
}

void VideoDriver_SDL::SetupKeyboard()
{

}

void VideoDriver_SDL::Stop()
{
	SDL_DestroyTexture(_sdl_screentext);
	SDL_DestroyRenderer(_sdl_renderer);
	SDL_DestroyWindow(_sdl_window);
}

void VideoDriver_SDL::MainLoop()
{
	DEBUG(misc, 1, "MainLoop.\n");
	uint32 cur_ticks = SDL_CALL SDL_GetTicks();
	uint32 last_cur_ticks = cur_ticks;
	uint32 next_tick = cur_ticks + MILLISECONDS_PER_TICK;
	uint32 mod;
	int numkeys;

	CheckPaletteAnim();

	if (_draw_threaded) {
		/* Initialise the mutex first, because that's the thing we *need*
		 * directly in the newly created thread. */
		_draw_mutex = ThreadMutex::New();
		if (_draw_mutex == NULL) {
			_draw_threaded = false;
		} else {
			_draw_mutex->BeginCritical();
			_draw_continue = true;

			_draw_threaded = ThreadObject::New(&DrawSurfaceToScreenThread, NULL, &_draw_thread, "ottd:draw-sdl");

			/* Free the mutex if we won't be able to use it. */
			if (!_draw_threaded) {
				_draw_mutex->EndCritical();
				delete _draw_mutex;
				_draw_mutex = NULL;
			} else {
				/* Wait till the draw mutex has started itself. */
				_draw_mutex->WaitForSignal();
			}
		}
	}

	DEBUG(driver, 1, "SDL: using %sthreads", _draw_threaded ? "" : "no ");

	for (;;) {
		uint32 prev_cur_ticks = cur_ticks; // to check for wrapping
		InteractiveRandom(); // randomness

		HandleAnalogSticks();
		FinishSimulatedMouseClicks();
		while (PollEvent() == -1) {}
		if (_exit_game) break;

		mod = SDL_CALL SDL_GetModState();
#if SDL_VERSION_ATLEAST(1, 3, 0)
		const Uint8 *keys = SDL_CALL SDL_GetKeyboardState(&numkeys);
#else
		keys = SDL_CALL SDL_GetKeyState(&numkeys);
#endif
#if defined(_DEBUG)
		if (_shift_pressed)
#else
		/* Speedup when pressing tab, except when using ALT+TAB
		 * to switch to another application */
#if SDL_VERSION_ATLEAST(1, 3, 0)
		if (keys[SDL_SCANCODE_TAB] && (mod & KMOD_ALT) == 0)
#else
		if (keys[SDLK_TAB] && (mod & KMOD_ALT) == 0)
#endif /* SDL_VERSION_ATLEAST(1, 3, 0) */
#endif /* defined(_DEBUG) */
		{
			if (!_networking && _game_mode != GM_MENU) _fast_forward |= 2;
		} else if (_fast_forward & 2) {
			_fast_forward = 0;
		}

		cur_ticks = SDL_CALL SDL_GetTicks();
		if (cur_ticks >= next_tick || (_fast_forward && !_pause_mode) || cur_ticks < prev_cur_ticks) {
			_realtime_tick += cur_ticks - last_cur_ticks;
			last_cur_ticks = cur_ticks;
			next_tick = cur_ticks + MILLISECONDS_PER_TICK;

			bool old_ctrl_pressed = _ctrl_pressed;

			_ctrl_pressed  = !!(mod & KMOD_CTRL);
			_shift_pressed = !!(mod & KMOD_SHIFT);

			/* determine which directional keys are down */
#if SDL_VERSION_ATLEAST(1, 3, 0)
			int down = keys[SDL_SCANCODE_DOWN];
			int left = keys[SDL_SCANCODE_LEFT];
			int up = keys[SDL_SCANCODE_UP];
			int right = keys[SDL_SCANCODE_RIGHT];
#else
			int down = keys[SDLK_DOWN];
			int left = keys[SDLK_LEFT];
			int up = keys[SDLK_UP];
			int right = keys[SDLK_RIGHT];
#endif
			if (down && up) {
				up = 0;
			}
			if (left && right) {
				right = 0;
			}
			_dirkeys |= ((!_pressed_cursor_keys[0] && down) ? OTTD_DIR_DOWN : 0) |
						((!_pressed_cursor_keys[1] && left) ? OTTD_DIR_LEFT : 0) |
						((!_pressed_cursor_keys[2] && up) ? OTTD_DIR_UP : 0) |
						((!_pressed_cursor_keys[3] && right) ? OTTD_DIR_RIGHT : 0);


			uint8 tmp = ((_pressed_cursor_keys[0] && !down) ? OTTD_DIR_DOWN : 0) |
						((_pressed_cursor_keys[1] && !left) ? OTTD_DIR_LEFT : 0) |
						((_pressed_cursor_keys[2] && !up) ? OTTD_DIR_UP : 0) |
						((_pressed_cursor_keys[3] && !right) ? OTTD_DIR_RIGHT : 0);
			_dirkeys = _dirkeys &~tmp;

			_pressed_cursor_keys[0] = down;
			_pressed_cursor_keys[1] = left;
			_pressed_cursor_keys[2] = up;
			_pressed_cursor_keys[3] = right;

			if (old_ctrl_pressed != _ctrl_pressed) HandleCtrlChanged();

			/* The gameloop is the part that can run asynchronously. The rest
			 * except sleeping can't. */
			if (_draw_mutex != NULL) _draw_mutex->EndCritical();

			GameLoop();

			if (_draw_mutex != NULL) _draw_mutex->BeginCritical();

			UpdateWindows();
			_local_palette = _cur_palette;
		} else {
			/* Release the thread while sleeping */
			if (_draw_mutex != NULL) _draw_mutex->EndCritical();
			CSleep(1);
			if (_draw_mutex != NULL) _draw_mutex->BeginCritical();

			NetworkDrawChatMessage();
			DrawMouseCursor();
		}

		/* End of the critical part. */
		if (_draw_mutex != NULL && !HasModalProgress()) {
			_draw_mutex->SendSignal();
		} else {
			/* Oh, we didn't have threads, then just draw unthreaded */
			CheckPaletteAnim();
			DrawSurfaceToScreen();
		}
	}

	if (_draw_mutex != NULL) {
		_draw_continue = false;
		/* Sending signal if there is no thread blocked
		 * is very valid and results in noop */
		_draw_mutex->SendSignal();
		_draw_mutex->EndCritical();
		_draw_thread->Join();

		delete _draw_mutex;
		delete _draw_thread;

		_draw_mutex = NULL;
		_draw_thread = NULL;
	}
}

bool VideoDriver_SDL::ChangeResolution(int w, int h)
{
	if (_draw_mutex != NULL) _draw_mutex->BeginCritical(true);
	bool ret = CreateMainSurface(w, h);
	if (_draw_mutex != NULL) _draw_mutex->EndCritical(true);
	return ret;
}

bool VideoDriver_SDL::ToggleFullscreen(bool fullscreen)
{
	if (_draw_mutex != NULL) _draw_mutex->BeginCritical(true);
	_fullscreen = fullscreen;
	GetVideoModes(); // get the list of available video modes
	bool ret = _num_resolutions != 0 && CreateMainSurface(_cur_resolution.width, _cur_resolution.height);

	if (!ret) {
		/* switching resolution failed, put back full_screen to original status */
		_fullscreen ^= true;
	}

	if (_draw_mutex != NULL) _draw_mutex->EndCritical(true);
	return ret;
}

bool VideoDriver_SDL::AfterBlitterChange()
{
	return CreateMainSurface(_screen.width, _screen.height);
}

void VideoDriver_SDL::AcquireBlitterLock()
{
	if (_draw_mutex != NULL) _draw_mutex->BeginCritical(true);
}

void VideoDriver_SDL::ReleaseBlitterLock()
{
	if (_draw_mutex != NULL) _draw_mutex->EndCritical(true);
}

#endif /* WITH_SDL */
