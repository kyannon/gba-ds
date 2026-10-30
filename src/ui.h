/* Windows, physical screens, input and drawing shared by the library and the game. */
#ifndef GBADS_UI_H
#define GBADS_UI_H

#include "config.h"
#include "textfont.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
	SDL_Window* win;
	SDL_Renderer* ren;
	/* Android's second screen (win is NULL): drawn at ui_present from
	 * recorded calls (ui.c), with the game screen's renderer into the
	 * second display's window (GPU), or failing that with a software
	 * renderer into surf, which is then copied over. */
	bool offwin;
	SDL_Surface* surf;
	bool retained; /* drawn from recorded calls at ui_present, only when they change (Android) */
} View;

/* A physical screen: a rectangle inside one view's output. */
typedef struct {
	int view;
	SDL_Rect rect;
} Screen;

extern View views[2];
extern int nViews;
extern Screen screens[2];
extern int nScreens;

/* Logical buttons (after face-button mapping). */
enum {
	B_A, B_B, B_X, B_Y, B_START, B_SELECT, B_HOME, B_L, B_R, B_L2, B_R2, B_L3,
	B_UP, B_DOWN, B_LEFT, B_RIGHT, B_COUNT
};
extern bool btnNow[B_COUNT], btnPrev[B_COUNT];
static inline bool btn_pressed(int b) {
	return btnNow[b] && !btnPrev[b];
}
static inline bool btn_released(int b) {
	return !btnNow[b] && btnPrev[b];
}

/* Pointer (touch or mouse) event in screen-local pixels. */
typedef struct {
	int kind; /* 0 none, 1 down, 2 up, 3 motion */
	int screen;
	int x, y;
} Pointer;

bool ui_init(Config* cfg, bool wantDual);
void ui_shutdown(void);
/* a renderer reset since the last call (SDL_RENDER_*_RESET) */
bool ui_render_reset(void);
/* ui.c's own textures let go, to be made again (after a wake) */
void ui_textures_reset(void);
void ui_layout(void);
/* Call once per loop iteration: sway fixups, script ticks. */
void ui_tick(void);
uint64_t ui_ticks(void);

int ui_touch_screen(void); /* index into screens[] */
int ui_other_screen(void); /* the non-touch screen, or the touch screen if there's only one */

/* Pump SDL events. Returns false when the app should quit. Pointer events are
 * queued for ui_next_pointer(); window changes re-run the layout. */
bool ui_events(void);
/* Called from ui_events when the app is about to be put in the background
 * (Android: Home, the task switcher, the screen turning off...), where it
 * may be killed without notice. NULL: nothing. */
void ui_on_background(void (*fn)(void));
/* Called from ui_events when the app is back in the foreground. NULL: nothing. */
void ui_on_foreground(void (*fn)(void));
bool ui_next_pointer(Pointer* p);
void ui_read_buttons(void);

/* Input script for automated testing (see config_usage). */
void ui_load_script(const char* path);

void ui_clear(void);
void ui_present(void);
/* Which views the last ui_present drew (bit 0: view 0, bit 1: view 1; on
 * Android a view whose picture didn't change isn't redrawn). */
int ui_last_redraws(void);
/* Time the last ui_present spent drawing / presenting a view (ms). */
double ui_last_present_ms(int view);

/* Android: the view whose presents should follow its display's refresh
 * (the one the game is on: each frame is shown, none dropped when one comes
 * a little late); -1 for none. Elsewhere: nothing. */
void ui_sync_view(int view);
bool ui_script_quiet(void);
/* The refresh rate (Hz) of the display a view is on; 0 if unknown. */
double ui_display_hz(int view);

/* Android: the next ui_present draws `first` (the game's view) first, and
 * puts off redrawing the other view to the next frame (at most a few in a
 * row) if the frame, started at frameStart (performance counter), has
 * already used budgetMs: an animation step a frame late isn't seen, a late
 * game frame is. first < 0: no ordering. Elsewhere: nothing. */
void ui_present_budget(int first, Uint64 frameStart, double budgetMs);

SDL_Renderer* ui_ren(int screen);
/* Draws a texture (one per view, indexed by view) letterboxed into a screen area. */
void ui_image(int screen, SDL_Texture* const* texPerView, int w, int h, const char* mode, const SDL_Rect* areaLocal);
/* Pixel-art drawing for the game and map: whole-number prescale (sharp at any
 * size), optional LCD grid, then filtered to the final size on the GPU.
 * lcd: 0 no grid, 1 a cell per pixel, n a cell per n x n pixels (a picture
 * made at n times the GBA's resolution, like Mario Kart's HUD screen). */
void ui_image_pixels(int screen, SDL_Texture* const* texPerView, int w, int h, const char* mode, int lcd);
/* Where ui_image_pixels puts a w x h image on a screen (screen-local coords). */
SDL_Rect ui_image_rect(int screen, int w, int h, const char* mode);
void ui_fill(int screen, SDL_Rect local, SDL_Color c);
/* Clip drawing to a rectangle (renderer coordinates), or NULL for none. */
void ui_clip(int screen, const SDL_Rect* clip);
/* SDL_UpdateTexture (whole texture) that ui.c notices: use it for anything
 * drawn on the screens, so the second screen on Android redraws. */
void ui_update_texture(SDL_Texture* t, const void* px, int pitch);
/* Filled box with rounded corners (radius in pixels). */
void ui_fill_round(int screen, SDL_Rect local, SDL_Color c, int radius);
/* Text in Jost (textfont.h: TF_BOLD for labels and headings, TF_BODY for
 * longer text), any UTF-8 string, capitals capPx tall and centred on cy.
 * align: -1 starts at x, 0 is centred on x, 1 ends at x. */
void ui_font_text(int screen, int font, int x, int cy, int capPx, const char* text, SDL_Color c, int align);
int ui_font_width(int font, int capPx, const char* text);
/* Word-wrapped to width, starting at the top-left x, y; returns the y below. */
int ui_font_wrap(int screen, int font, int x, int y, int width, int capPx, const char* text, SDL_Color c);
/* A title fitted into area (bold, centred, at most capMax tall, on one line
 * or broken in two at a space): a placeholder for a game's logo. */
void ui_title_block(int screen, SDL_Rect area, const char* text, int capMax, SDL_Color c);
/* Labels: bold, centred on cx, cy. */
void ui_label(int screen, int cx, int cy, int capPx, const char* text, SDL_Color c);
int ui_label_width(int screen, int capPx, const char* text); /* width ui_label draws */
void ui_dim(int screen, Uint8 alpha);
void ui_text(int screen, int x, int y, int px, const char* s, SDL_Color c);
int ui_text_width(const char* s, int px);
void ui_text_center(int screen, int cx, int y, int px, const char* s, SDL_Color c);
/* Pixel size for text so that it is legible on this screen. */
float ui_px(int screen); /* the layout unit: screen height / 192 (4 at 768), at least 2 */

/* Loads a BMP into one texture per view. Returns false if missing. */
bool ui_load_image(const char* path, SDL_Texture* out[2], int* w, int* h, bool smooth);
/* One of the built-in images (BRAND_* in brand.h), smooth-scaled. */
bool ui_load_brand(int id, SDL_Texture* out[2], int* w, int* h);
void ui_free_image(SDL_Texture* tex[2]);
/* A game's title logo: <asset_dir>/logos/<CODE>.png|.bmp if the user put one
 * there, else the one extracted from the ROM (<state_dir>/logos/<CODE>.png),
 * which is queued for extraction in the background if it doesn't exist yet
 * (watch logo_generation() and call again). pixelArt: draw with "integer". */
/* The game's logo as an RGBA32 surface (caller frees): the user's file if
 * there is one, else the one extracted from the title screen; NULL if
 * neither exists yet. */
SDL_Surface* ui_logo_surface(const Config* c, const char* code, bool* pixelArt);
bool ui_load_logo(const Config* cfg, const char* rom, const char* code, SDL_Texture* tex[2], int* w, int* h,
                  bool* pixelArt);

void ui_screenshot(const char* path);
/* An input script's LOAD:file, once (NULL if none is due): the play session
 * loads that state as a quick load would, the second screen kept. */
const char* ui_script_load(void);
/* The screens as currently drawn (call before ui_present), stacked top to
 * bottom in screen order, saved as PNG. */
bool ui_screenshot_stacked(const char* pngPath);
void ui_osd(const char* fmt, ...);
int ui_volume(void); /* GBA DS's own sound volume, 0-100 (100 unless launch.sh asks for one) */
const char* ui_osd_text(void); /* NULL when expired */

/* Raw controller input for the button mapper (mapper.c): queued from
 * ui_events while capturing. kind: 'b' button down, 'h' hat (value: the
 * SDL_HAT_* direction), 'a' axis motion (value), 'k' a key (index: its
 * scancode; keys can't be mapped). */
typedef struct {
	int kind;
	SDL_JoystickID which;
	int index, value;
} RawInput;
void ui_raw_capture(bool on);
bool ui_next_raw(RawInput* r);
/* Where the mappings made with the button mapper are kept (loaded at start). */
void ui_mapping_file(char* out, size_t n);
/* Opens controllers that have a mapping now. */
void ui_reopen_pads(void);
/* The controller with this GUID has a mapping made with the button mapper
 * (by position, so Android's label-based A/B swap doesn't apply to it). */
void ui_mapped_by_user(const char* guid);

extern volatile int uiQuit; /* set by signals */

#endif
