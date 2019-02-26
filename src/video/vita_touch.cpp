#include <psp2/kernel/processmgr.h>
#include "vita.h"

#define NO_TOUCH -1 // finger id setting if finger is not touching the screen

#include "vita_touch.h"

#include "math.h"

static void init_touch(void);
static void preprocess_events(SDL_Event *event);
static void preprocess_finger_down(SDL_Event *event);
static void preprocess_finger_up(SDL_Event *event);
static void preprocess_finger_motion(SDL_Event *event);
static void set_mouse_button_event(SDL_Event *event, uint32_t type, uint8_t button, int32_t x, int32_t y);

static int _vita_rear_touch = 0; // always disable rear_touch for now
extern int _last_mouse_x;
extern int _last_mouse_y;

static int _touch_initialized = 0;
static unsigned int _simulated_click_start_time[SCE_TOUCH_PORT_MAX_NUM][2]; // initiation time of last simulated left or right click (zero if no click)

enum {
	MAX_NUM_FINGERS = 3, // number of fingers to track per panel
	MAX_TAP_TIME = 250, // taps longer than this will not result in mouse click events
	MAX_TAP_MOTION_DISTANCE = 10, // max distance finger motion in Vita screen pixels to be considered a tap
	SIMULATED_CLICK_DURATION = 50, // time in ms how long simulated mouse clicks should be
}; // track three fingers per panel

typedef struct {
	int id; // -1: not touching
	Uint32 time_last_down;
	int last_x; // last known screen coordinates
	int last_y; // last known screen coordinates
	float last_down_x; // SDL touch coordinates when last pressed down
	float last_down_y; // SDL touch coordinates when last pressed down
} Touch;

static Touch _finger[SCE_TOUCH_PORT_MAX_NUM][MAX_NUM_FINGERS]; // keep track of finger status

typedef enum {
	DRAG_NONE = 0,
	DRAG_TWO_FINGER,
	DRAG_THREE_FINGER,
} DraggingType;

static DraggingType _multi_finger_dragging[SCE_TOUCH_PORT_MAX_NUM]; // keep track whether we are currently drag-and-dropping

static void init_touch(void)
{
	for (int port = 0; port < SCE_TOUCH_PORT_MAX_NUM; port++) {
		for (int i = 0; i < MAX_NUM_FINGERS; i++) {
			_finger[port][i].id = NO_TOUCH;
		}
		_multi_finger_dragging[port] = DRAG_NONE;
	}

	for (int port = 0; port < SCE_TOUCH_PORT_MAX_NUM; port++) {
		for (int i = 0; i < 2; i++) {
			_simulated_click_start_time[port][i] = 0;
		}
	}
}

void HandleTouch(SDL_Event *event)
{
	if (!_touch_initialized) {
		init_touch();
		_touch_initialized = 1;
	}
	preprocess_events(event);
}

static void preprocess_events(SDL_Event *event)
{
	// prevent suspend
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);

	// Supported touch gestures:
	// left mouse click: single finger short tap
	// right mouse click: second finger short tap while first finger is still down
	// pointer motion: single finger drag
	// left button drag and drop: dual finger drag
	// right button drag and drop: triple finger drag
	if (event->type != SDL_FINGERDOWN && event->type != SDL_FINGERUP && event->type != SDL_FINGERMOTION)
		return;

	// front (0) or back (1) panel
	SDL_TouchID port = event->tfinger.touchId;
	if (port != 0 && (!_vita_rear_touch || port != 1))
		return;

	switch (event->type) {
		case SDL_FINGERDOWN:
			preprocess_finger_down(event);
			break;
		case SDL_FINGERUP:
			preprocess_finger_up(event);
			break;
		case SDL_FINGERMOTION:
			preprocess_finger_motion(event);
		break;
	}
}

static void preprocess_finger_down(SDL_Event *event)
{
	// front (0) or back (1) panel
	SDL_TouchID port = event->tfinger.touchId;
	// id (for multitouch)
	SDL_FingerID id = event->tfinger.fingerId;

	// make sure each finger is not reported down multiple times
	for (int i = 0; i < MAX_NUM_FINGERS; i++) {
		if (_finger[port][i].id != id)
			continue;
		_finger[port][i].id = NO_TOUCH;
	}

	// we need the timestamps to decide later if the user performed a short tap (click)
	// or a long tap (drag)
	// we also need the last coordinates for each finger to keep track of dragging
	for (int i = 0; i < MAX_NUM_FINGERS; i++) {
		if (_finger[port][i].id != NO_TOUCH)
			continue;
		_finger[port][i].id = id;
		_finger[port][i].time_last_down = event->tfinger.timestamp;
		_finger[port][i].last_down_x = event->tfinger.x;
		_finger[port][i].last_down_y = event->tfinger.y;
		_finger[port][i].last_x = event->tfinger.x * VITA_DISPLAY_WIDTH;
		_finger[port][i].last_y = event->tfinger.y * VITA_DISPLAY_HEIGHT;
		break;
	}
}

static void preprocess_finger_up(SDL_Event *event)
{
	// front (0) or back (1) panel
	SDL_TouchID port = event->tfinger.touchId;
	// id (for multitouch)
	SDL_FingerID id = event->tfinger.fingerId;

	// find out how many fingers were down before this event
	int num_fingers_down = 0;
	for (int i = 0; i < MAX_NUM_FINGERS; i++)
		if (_finger[port][i].id >= 0)
			num_fingers_down++;

	for (int i = 0; i < MAX_NUM_FINGERS; i++) {
		if (_finger[port][i].id != id)
			continue;
		_finger[port][i].id = NO_TOUCH;
		if (!_multi_finger_dragging[port]) {
			if ((event->tfinger.timestamp - _finger[port][i].time_last_down) > MAX_TAP_TIME)
				continue;

			// short (<MAX_TAP_TIME ms) tap is interpreted as right/left mouse click depending on # fingers already down
			// but only if the finger hasn't moved since it was pressed down by more than MAX_TAP_MOTION_DISTANCE pixels
			float xrel = ((event->tfinger.x * VITA_DISPLAY_WIDTH) - (_finger[port][i].last_down_x * VITA_DISPLAY_WIDTH));
			float yrel = ((event->tfinger.y * VITA_DISPLAY_HEIGHT) - (_finger[port][i].last_down_y * VITA_DISPLAY_HEIGHT));
			float max_r_squared = (float) (MAX_TAP_MOTION_DISTANCE * MAX_TAP_MOTION_DISTANCE);
			if ((xrel * xrel + yrel * yrel) >= max_r_squared)
				continue;

			if (num_fingers_down != 2 && num_fingers_down != 1)
				continue;

			Uint8 simulated_button = 0;
			if (num_fingers_down == 2) {
				simulated_button = SDL_BUTTON_RIGHT;
				// need to raise the button later
				_simulated_click_start_time[port][1] = event->tfinger.timestamp;
			} else if (num_fingers_down == 1) {
				simulated_button = SDL_BUTTON_LEFT;
				// need to raise the button later
				_simulated_click_start_time[port][0] = event->tfinger.timestamp;
			}
			set_mouse_button_event(event, SDL_MOUSEBUTTONDOWN, simulated_button, _last_mouse_x, _last_mouse_y);
		} else if (num_fingers_down == 1) {
			// when dragging, and the last finger is lifted, the drag is over
			Uint8 simulated_button = 0;
			if (_multi_finger_dragging[port] == DRAG_THREE_FINGER)
				simulated_button = SDL_BUTTON_RIGHT;
			else {
				simulated_button = SDL_BUTTON_LEFT;
			}
			set_mouse_button_event(event, SDL_MOUSEBUTTONUP, simulated_button, _last_mouse_x, _last_mouse_y);
			_multi_finger_dragging[port] = DRAG_NONE;
		}
	}
}

static void preprocess_finger_motion(SDL_Event *event)
{
	// front (0) or back (1) panel
	SDL_TouchID port = event->tfinger.touchId;
	// id (for multitouch)
	SDL_FingerID id = event->tfinger.fingerId;

	// find out how many fingers were down before this event
	int num_fingers_down = 0;
	for (int i = 0; i < MAX_NUM_FINGERS; i++)
		if (_finger[port][i].id >= 0)
			num_fingers_down++;

	if (num_fingers_down == 0)
		return;

	// If we are starting a multi-finger drag, start holding down the mouse button
	if (num_fingers_down >= 2 && !_multi_finger_dragging[port]) {
		// only start a multi-finger drag if at least two fingers have been down long enough
		int num_fingers_downlong = 0;
		for (int i = 0; i < MAX_NUM_FINGERS; i++) {
			if (_finger[port][i].id == NO_TOUCH)
				continue;
			if (event->tfinger.timestamp - _finger[port][i].time_last_down > MAX_TAP_TIME)
				num_fingers_downlong++;
		}
		if (num_fingers_downlong >= 2) {
			Uint8 simulated_button = 0;
			if (num_fingers_downlong == 2) {
				simulated_button = SDL_BUTTON_LEFT;
				_multi_finger_dragging[port] = DRAG_TWO_FINGER;
			} else {
				simulated_button = SDL_BUTTON_RIGHT;
				_multi_finger_dragging[port] = DRAG_THREE_FINGER;
			}
			SDL_Event ev;
			set_mouse_button_event(&ev, SDL_MOUSEBUTTONDOWN, simulated_button, _last_mouse_x, _last_mouse_y);
			SDL_PushEvent(&ev);
		}
	}

	//check if this is the "oldest" finger down (or the only finger down), otherwise it will not affect mouse motion
	bool update_pointer = true;
	if (num_fingers_down > 1) {
		for (int i = 0; i < MAX_NUM_FINGERS; i++) {
			if (_finger[port][i].id != id)
				continue;
			for (int j = 0; j < MAX_NUM_FINGERS; j++) {
				if (_finger[port][j].id == NO_TOUCH || (j == i))
					continue;
				if (_finger[port][j].time_last_down < _finger[port][i].time_last_down)
					update_pointer = false;
			}
		}
	}
	if (!update_pointer)
		return;

	int x = event->tfinger.x * VITA_DISPLAY_WIDTH;
	int y = event->tfinger.y * VITA_DISPLAY_HEIGHT;
	int xrel = 0;
	int yrel = 0;

	// find delta and update the current finger's coordinates so we can track it later
	for (int i = 0; i < MAX_NUM_FINGERS; i++) {
		if (_finger[port][i].id != id)
			continue;
		xrel = x - _finger[port][i].last_x;
		yrel = y - _finger[port][i].last_y;
		_finger[port][i].last_x = x;
		_finger[port][i].last_y = y;
	}

	if (!xrel && !yrel)
		return;
	// limit joystick mouse to screen coords, same as physical mouse
	x = _last_mouse_x + xrel;
	y = _last_mouse_y + yrel;
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
	event->type = SDL_MOUSEMOTION;
	event->motion.x = x;
	event->motion.y = y;
	event->motion.xrel = xrel;
	event->motion.yrel = yrel;
}

void FinishSimulatedMouseClicks()
{
	for (int port = 0; port < SCE_TOUCH_PORT_MAX_NUM; port++) {
		for (int i = 0; i < 2; i++) {
			if (_simulated_click_start_time[port][i] == 0)
				continue;

			Uint32 current_time = SDL_GetTicks();
			if (current_time - _simulated_click_start_time[port][i] < SIMULATED_CLICK_DURATION)
				continue;

			int simulated_button;
			if (i == 0)
				simulated_button = SDL_BUTTON_LEFT;
			else
				simulated_button = SDL_BUTTON_RIGHT;
			SDL_Event ev;
			set_mouse_button_event(&ev, SDL_MOUSEBUTTONUP, simulated_button, _last_mouse_x, _last_mouse_y);
			SDL_PushEvent(&ev);

			_simulated_click_start_time[port][i] = 0;
		}
	}
}

static void set_mouse_button_event(SDL_Event *event, uint32_t type, uint8_t button, int32_t x, int32_t y)
{
	event->type = type;
	event->button.button = button;
	if (type == SDL_MOUSEBUTTONDOWN)
		event->button.state = SDL_PRESSED;
	else
		event->button.state = SDL_RELEASED;
	event->button.x = x;
	event->button.y = y;
}
