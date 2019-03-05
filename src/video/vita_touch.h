#ifndef VITA_TOUCH_H
#define VITA_TOUCH_H

#include <SDL2/SDL.h>
#ifdef __vita__
#include <psp2/touch.h>
#endif
#include <stdbool.h>

void HandleTouch(SDL_Event *event);
void FinishSimulatedMouseClicks(void);

#endif /* VITA_TOUCH_H */
