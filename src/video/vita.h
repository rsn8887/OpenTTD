#ifndef VITA_H
#define VITA_H

#define VITA_DISPLAY_WIDTH 960

#if defined(__vita__)
#define VITA_DISPLAY_HEIGHT 544
#else
#define VITA_DISPLAY_HEIGHT 540
#endif

#endif // VITA_H