/* SDL2 configuration for GBA DS's web build (wasm32-wasi, no threads):
 * the offscreen video driver drawn with the software renderer, whose
 * window framebuffer the page copies to its canvas; keyboard input pushed
 * in by the page (web/web.c); no audio, no real controllers. SDL's own C
 * library stands in (no HAVE_LIBC), timing comes from web/sdl_timer_web.c. */
#ifndef SDL_config_wasi_h_
#define SDL_config_wasi_h_
#define SDL_config_h_

#include "SDL_platform.h"

#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_GCC_SYNC_LOCK_TEST_AND_SET 1

#define SDL_AUDIO_DISABLED 1
#define SDL_JOYSTICK_DUMMY 1
#define SDL_HAPTIC_DISABLED 1
#define SDL_HIDAPI_DISABLED 1
#define SDL_SENSOR_DISABLED 1
#define SDL_LOADSO_DISABLED 1
#define SDL_THREADS_DISABLED 1
#define SDL_POWER_DISABLED 1
#define SDL_FILESYSTEM_DUMMY 1
#define SDL_VIDEO_DRIVER_OFFSCREEN 1
#define SDL_VIDEO_RENDER_SW 1
#define DYNAPI_NEEDS_DLOPEN 1 /* (and no dlopen: so no dynamic API layer, one static library) */

#endif
