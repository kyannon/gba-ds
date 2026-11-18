/* The part of SDL2 that GBA DS uses, for the Nintendo 3DS: declarations only
 * (../sdl_ctr.c draws with the GPU through citro2d, reads the buttons and
 * the touch screen, and plays sound through ndsp). The two screens are two
 * displays, each with a window of its size: the top one (400x240) first.
 * The names and values are SDL 2's own. */
#ifndef GBADS_CTR_SDL_H
#define GBADS_CTR_SDL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef unsigned int Uint32;
typedef int Sint32;
typedef uint64_t Uint64;
typedef int64_t Sint64;
typedef enum { SDL_FALSE, SDL_TRUE } SDL_bool;

#define SDL_max(a, b) (((a) > (b)) ? (a) : (b))
#define SDL_min(a, b) (((a) < (b)) ? (a) : (b))
#define SDL_clamp(x, a, b) (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x)))
#define SDL_arraysize(a) (sizeof(a) / sizeof((a)[0]))
#define SDL_free free
#define SDL_tolower tolower
#define SDL_toupper toupper

typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_Point { int x, y; } SDL_Point;
typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;

static inline SDL_bool SDL_PointInRect(const SDL_Point* p, const SDL_Rect* r) {
	return p->x >= r->x && p->x < r->x + r->w && p->y >= r->y && p->y < r->y + r->h ? SDL_TRUE : SDL_FALSE;
}
SDL_bool SDL_IntersectRect(const SDL_Rect* a, const SDL_Rect* b, SDL_Rect* out);

/* ------------------------------------------------------------ the basics */
#define SDL_INIT_AUDIO 0x10u
#define SDL_INIT_VIDEO 0x20u
#define SDL_INIT_GAMECONTROLLER 0x2000u
#define SDL_INIT_EVENTS 0x4000u
int SDL_Init(Uint32 flags);
void SDL_Quit(void);
const char* SDL_GetError(void);
const char* SDL_GetPlatform(void);
#define SDL_HINT_APP_NAME "SDL_APP_NAME"
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_HINT_ORIENTATIONS "SDL_IOS_ORIENTATIONS"
#define SDL_HINT_ANDROID_TRAP_BACK_BUTTON "SDL_ANDROID_TRAP_BACK_BUTTON"
#define SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"
SDL_bool SDL_SetHint(const char* name, const char* value);
#define SDL_DISABLE 0
int SDL_ShowCursor(int toggle);

Uint32 SDL_GetTicks(void);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);
void SDL_Delay(Uint32 ms);

typedef enum { SDL_POWERSTATE_UNKNOWN, SDL_POWERSTATE_ON_BATTERY, SDL_POWERSTATE_NO_BATTERY, SDL_POWERSTATE_CHARGING, SDL_POWERSTATE_CHARGED } SDL_PowerState;
SDL_PowerState SDL_GetPowerInfo(int* seconds, int* percent);

typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_mutex SDL_mutex;
typedef int (*SDL_ThreadFunction)(void* data);
SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char* name, void* data);
void SDL_DetachThread(SDL_Thread* thread);
SDL_mutex* SDL_CreateMutex(void);
int SDL_LockMutex(SDL_mutex* m);
int SDL_UnlockMutex(SDL_mutex* m);

/* ------------------------------------------------------------ surfaces */
#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_PIXELFORMAT_ABGR8888 0x16762004u
#define SDL_PIXELFORMAT_XBGR8888 0x16561804u
#define SDL_PIXELFORMAT_RGBA32 SDL_PIXELFORMAT_ABGR8888
typedef struct SDL_PixelFormat {
	Uint32 format;
	Uint8 BitsPerPixel, BytesPerPixel;
} SDL_PixelFormat;
typedef struct SDL_Surface {
	Uint32 flags;
	SDL_PixelFormat* format;
	int w, h;
	int pitch;
	void* pixels;
	SDL_PixelFormat formatStore;
	bool ownsPixels;
} SDL_Surface;
SDL_Surface* SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h, int depth, Uint32 format);
SDL_Surface* SDL_CreateRGBSurfaceWithFormatFrom(void* pixels, int w, int h, int depth, int pitch, Uint32 format);
void SDL_FreeSurface(SDL_Surface* s);
SDL_Surface* SDL_DuplicateSurface(SDL_Surface* s);
static inline int SDL_LockSurface(SDL_Surface* s) { (void) s; return 0; }
static inline void SDL_UnlockSurface(SDL_Surface* s) { (void) s; }
Uint32 SDL_MapRGBA(const SDL_PixelFormat* f, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
SDL_Surface* SDL_LoadBMP(const char* path);
int SDL_SaveBMP(SDL_Surface* s, const char* path);

/* ------------------------------------------------------------ video */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_DisplayMode { Uint32 format; int w, h, refresh_rate; void* driverdata; } SDL_DisplayMode;
typedef struct SDL_RendererInfo { const char* name; Uint32 flags; Uint32 num_texture_formats; Uint32 texture_formats[16]; int max_texture_width, max_texture_height; } SDL_RendererInfo;
typedef enum { SDL_BLENDMODE_NONE = 0, SDL_BLENDMODE_BLEND = 1, SDL_BLENDMODE_ADD = 2, SDL_BLENDMODE_MOD = 4 } SDL_BlendMode;
typedef enum { SDL_ScaleModeNearest, SDL_ScaleModeLinear, SDL_ScaleModeBest } SDL_ScaleMode;
#define SDL_WINDOW_FULLSCREEN_DESKTOP 0x1001u
#define SDL_WINDOW_RESIZABLE 0x20u
#define SDL_WINDOW_ALLOW_HIGHDPI 0x2000u
#define SDL_WINDOWPOS_CENTERED_DISPLAY(d) (0x2FFF0000u | (d))
#define SDL_RENDERER_ACCELERATED 2u
#define SDL_RENDERER_PRESENTVSYNC 4u
#define SDL_RENDERER_TARGETTEXTURE 8u
#define SDL_TEXTUREACCESS_STATIC 0
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_TEXTUREACCESS_TARGET 2

int SDL_GetNumVideoDisplays(void);
const char* SDL_GetCurrentVideoDriver(void);
int SDL_GetDisplayBounds(int display, SDL_Rect* r);
const char* SDL_GetDisplayName(int display);
int SDL_GetDisplayDPI(int display, float* d, float* h, float* v);
int SDL_GetDesktopDisplayMode(int display, SDL_DisplayMode* m);
int SDL_GetCurrentDisplayMode(int display, SDL_DisplayMode* m);
int SDL_GetNumTouchDevices(void);
void* SDL_GL_GetProcAddress(const char* name);

SDL_Window* SDL_CreateWindow(const char* title, int x, int y, int w, int h, Uint32 flags);
void SDL_DestroyWindow(SDL_Window* w);
Uint32 SDL_GetWindowID(SDL_Window* w);
int SDL_GetWindowDisplayIndex(SDL_Window* w);
void SDL_GetWindowSize(SDL_Window* win, int* w, int* h);

SDL_Renderer* SDL_CreateRenderer(SDL_Window* w, int index, Uint32 flags);
SDL_Renderer* SDL_CreateSoftwareRenderer(SDL_Surface* s);
void SDL_DestroyRenderer(SDL_Renderer* r);
int SDL_GetRendererInfo(SDL_Renderer* r, SDL_RendererInfo* info);
int SDL_GetRendererOutputSize(SDL_Renderer* r, int* w, int* h);
int SDL_RenderSetVSync(SDL_Renderer* r, int vsync);
int SDL_SetRenderDrawBlendMode(SDL_Renderer* r, SDL_BlendMode mode);
int SDL_SetRenderDrawColor(SDL_Renderer* ren, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
int SDL_RenderClear(SDL_Renderer* r);
void SDL_RenderPresent(SDL_Renderer* r);
int SDL_RenderFlush(SDL_Renderer* r);
int SDL_RenderSetClipRect(SDL_Renderer* r, const SDL_Rect* rect);
void SDL_RenderGetClipRect(SDL_Renderer* r, SDL_Rect* rect);
SDL_bool SDL_RenderIsClipEnabled(SDL_Renderer* r);
int SDL_RenderSetViewport(SDL_Renderer* r, const SDL_Rect* rect);
int SDL_RenderFillRect(SDL_Renderer* r, const SDL_Rect* rect);
int SDL_RenderFillRects(SDL_Renderer* r, const SDL_Rect* rects, int n);
int SDL_RenderCopy(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* src, const SDL_Rect* dst);
int SDL_RenderReadPixels(SDL_Renderer* r, const SDL_Rect* rect, Uint32 format, void* pixels, int pitch);
SDL_bool SDL_RenderTargetSupported(SDL_Renderer* r);
int SDL_SetRenderTarget(SDL_Renderer* r, SDL_Texture* t);

SDL_Texture* SDL_CreateTexture(SDL_Renderer* r, Uint32 format, int access, int w, int h);
SDL_Texture* SDL_CreateTextureFromSurface(SDL_Renderer* r, SDL_Surface* s);
void SDL_DestroyTexture(SDL_Texture* t);
int SDL_QueryTexture(SDL_Texture* t, Uint32* format, int* access, int* w, int* h);
int SDL_UpdateTexture(SDL_Texture* t, const SDL_Rect* rect, const void* pixels, int pitch);
int SDL_SetTextureBlendMode(SDL_Texture* t, SDL_BlendMode mode);
int SDL_SetTextureScaleMode(SDL_Texture* t, SDL_ScaleMode mode);
int SDL_SetTextureColorMod(SDL_Texture* t, Uint8 r, Uint8 g, Uint8 b);
int SDL_SetTextureAlphaMod(SDL_Texture* t, Uint8 a);

/* ------------------------------------------------------------ sound */
typedef Uint32 SDL_AudioDeviceID;
typedef Uint16 SDL_AudioFormat;
#define AUDIO_S16SYS 0x8010
typedef struct SDL_AudioSpec {
	int freq;
	SDL_AudioFormat format;
	Uint8 channels, silence;
	Uint16 samples, padding;
	Uint32 size;
	void (*callback)(void*, Uint8*, int);
	void* userdata;
} SDL_AudioSpec;
typedef struct SDL_AudioStream SDL_AudioStream;
int SDL_AudioInit(const char* driver);
void SDL_AudioQuit(void);
const char* SDL_GetCurrentAudioDriver(void);
SDL_AudioDeviceID SDL_OpenAudioDevice(const char* device, int capture, const SDL_AudioSpec* want, SDL_AudioSpec* have, int changes);
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause);
int SDL_QueueAudio(SDL_AudioDeviceID dev, const void* data, Uint32 len);
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev);
void SDL_ClearQueuedAudio(SDL_AudioDeviceID dev);
/* (no conversion here: the DSP plays at the rate the game makes its sound,
 * and the queue's size is given as if it had been converted) */
SDL_AudioStream* SDL_NewAudioStream(SDL_AudioFormat sf, Uint8 sc, int srate, SDL_AudioFormat df, Uint8 dc, int drate);
int SDL_AudioStreamPut(SDL_AudioStream* s, const void* buf, int len);
int SDL_AudioStreamGet(SDL_AudioStream* s, void* buf, int len);
void SDL_FreeAudioStream(SDL_AudioStream* s);

/* ------------------------------------------------------------ input */
typedef Sint32 SDL_JoystickID;
typedef struct SDL_Joystick SDL_Joystick;
typedef struct SDL_GameController SDL_GameController;
typedef struct { Uint8 data[16]; } SDL_JoystickGUID;
typedef Sint32 SDL_Keycode;
typedef enum {
	SDL_SCANCODE_UNKNOWN = 0,
	SDL_SCANCODE_A = 4, SDL_SCANCODE_Q = 20, SDL_SCANCODE_S = 22, SDL_SCANCODE_X = 27, SDL_SCANCODE_Z = 29,
	SDL_SCANCODE_RETURN = 40, SDL_SCANCODE_ESCAPE = 41, SDL_SCANCODE_BACKSPACE = 42, SDL_SCANCODE_TAB = 43, SDL_SCANCODE_SPACE = 44,
	SDL_SCANCODE_F1 = 58, SDL_SCANCODE_HOME = 74, SDL_SCANCODE_RIGHT = 79, SDL_SCANCODE_LEFT = 80, SDL_SCANCODE_DOWN = 81, SDL_SCANCODE_UP = 82,
	SDL_SCANCODE_APPLICATION = 101, SDL_SCANCODE_MENU = 118, SDL_SCANCODE_VOLUMEUP = 128, SDL_SCANCODE_VOLUMEDOWN = 129,
	SDL_SCANCODE_RSHIFT = 229, SDL_SCANCODE_AC_HOME = 269, SDL_SCANCODE_AC_BACK = 270,
	SDL_NUM_SCANCODES = 512
} SDL_Scancode;
typedef enum {
	SDL_CONTROLLER_BUTTON_INVALID = -1,
	SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
	SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START,
	SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
	SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
	SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
	SDL_CONTROLLER_BUTTON_MISC1, SDL_CONTROLLER_BUTTON_PADDLE1, SDL_CONTROLLER_BUTTON_PADDLE2, SDL_CONTROLLER_BUTTON_PADDLE3,
	SDL_CONTROLLER_BUTTON_PADDLE4, SDL_CONTROLLER_BUTTON_TOUCHPAD, SDL_CONTROLLER_BUTTON_MAX
} SDL_GameControllerButton;
typedef enum {
	SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY,
	SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, SDL_CONTROLLER_AXIS_MAX
} SDL_GameControllerAxis;
typedef enum { SDL_JOYSTICK_TYPE_UNKNOWN, SDL_JOYSTICK_TYPE_GAMECONTROLLER } SDL_JoystickType;
#define SDL_HAT_CENTERED 0
#define SDL_HAT_UP 1
#define SDL_HAT_RIGHT 2
#define SDL_HAT_DOWN 4
#define SDL_HAT_LEFT 8
#define SDL_TOUCH_MOUSEID ((Uint32) -1)

int SDL_NumJoysticks(void);
SDL_bool SDL_IsGameController(int index);
SDL_GameController* SDL_GameControllerOpen(int index);
const char* SDL_GameControllerName(SDL_GameController* c);
char* SDL_GameControllerMapping(SDL_GameController* c);
SDL_Joystick* SDL_GameControllerGetJoystick(SDL_GameController* c);
Uint8 SDL_GameControllerGetButton(SDL_GameController* c, SDL_GameControllerButton b);
Sint16 SDL_GameControllerGetAxis(SDL_GameController* c, SDL_GameControllerAxis a);
const char* SDL_GameControllerGetStringForButton(SDL_GameControllerButton b);
int SDL_GameControllerAddMapping(const char* mapping);
int SDL_GameControllerAddMappingsFromFile(const char* path);
SDL_Joystick* SDL_JoystickOpen(int index);
void SDL_JoystickClose(SDL_Joystick* j);
SDL_Joystick* SDL_JoystickFromInstanceID(SDL_JoystickID id);
const char* SDL_JoystickName(SDL_Joystick* j);
SDL_JoystickGUID SDL_JoystickGetGUID(SDL_Joystick* j);
void SDL_JoystickGetGUIDString(SDL_JoystickGUID guid, char* out, int n);
SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int index);
int SDL_JoystickNumButtons(SDL_Joystick* j);
int SDL_JoystickNumAxes(SDL_Joystick* j);
int SDL_JoystickNumHats(SDL_Joystick* j);
Uint8 SDL_JoystickGetButton(SDL_Joystick* j, int b);
Sint16 SDL_JoystickGetAxis(SDL_Joystick* j, int a);
Uint8 SDL_JoystickGetHat(SDL_Joystick* j, int h);
void SDL_JoystickUpdate(void);
int SDL_JoystickAttachVirtual(SDL_JoystickType type, int axes, int buttons, int hats);
int SDL_JoystickSetVirtualAxis(SDL_Joystick* j, int axis, Sint16 v);
int SDL_JoystickSetVirtualButton(SDL_Joystick* j, int button, Uint8 v);
int SDL_JoystickSetVirtualHat(SDL_Joystick* j, int hat, Uint8 v);
const Uint8* SDL_GetKeyboardState(int* n);
const char* SDL_GetKeyName(SDL_Keycode key);

/* ------------------------------------------------------------ events */
enum {
	SDL_QUIT = 0x100,
	SDL_APP_WILLENTERBACKGROUND = 0x103, SDL_APP_DIDENTERFOREGROUND = 0x106,
	SDL_WINDOWEVENT = 0x200,
	SDL_KEYDOWN = 0x300, SDL_KEYUP,
	SDL_MOUSEMOTION = 0x400, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP,
	SDL_JOYAXISMOTION = 0x600, SDL_JOYBALLMOTION, SDL_JOYHATMOTION, SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP, SDL_JOYDEVICEADDED, SDL_JOYDEVICEREMOVED,
	SDL_CONTROLLERAXISMOTION = 0x650, SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLERBUTTONUP, SDL_CONTROLLERDEVICEADDED,
	SDL_FINGERDOWN = 0x700, SDL_FINGERUP, SDL_FINGERMOTION,
	SDL_RENDER_TARGETS_RESET = 0x2000, SDL_RENDER_DEVICE_RESET
};
enum { SDL_WINDOWEVENT_RESIZED = 5, SDL_WINDOWEVENT_SIZE_CHANGED = 6 };
typedef struct { SDL_Scancode scancode; SDL_Keycode sym; Uint16 mod; } SDL_Keysym;
typedef union SDL_Event {
	Uint32 type;
	struct { Uint32 type, timestamp, windowID; Uint8 event; } window;
	struct { Uint32 type, timestamp, windowID; Uint8 state, repeat; SDL_Keysym keysym; } key;
	struct { Uint32 type, timestamp, windowID, which, state; Sint32 x, y, xrel, yrel; } motion;
	struct { Uint32 type, timestamp, windowID, which; Uint8 button, state, clicks; Sint32 x, y; } button;
	struct { Uint32 type, timestamp; SDL_JoystickID which; Uint8 axis; Sint16 value; } jaxis;
	struct { Uint32 type, timestamp; SDL_JoystickID which; Uint8 hat, value; } jhat;
	struct { Uint32 type, timestamp; SDL_JoystickID which; Uint8 button, state; } jbutton;
	struct { Uint32 type, timestamp; SDL_JoystickID which; Uint8 button, state; } cbutton;
	struct { Uint32 type, timestamp; Sint64 touchId, fingerId; float x, y, dx, dy, pressure; Uint32 windowID; } tfinger;
} SDL_Event;
int SDL_PollEvent(SDL_Event* ev);
int SDL_PushEvent(SDL_Event* ev);

#endif
