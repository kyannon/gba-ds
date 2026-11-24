#include "second_shm.h"

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__wasi__)
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* The file ($XDG_RUNTIME_DIR/gbads-second.shm, or GBADS_SECOND_SHM): a header,
 * then w x h pixels, ABGR8888 (SDL's byte order: R, G, B, A in memory).
 * The same header as the proof of concept's LD_PRELOAD shim (issue #86). `seq`
 * is a sequence lock: odd while the game is writing a frame, even (and one
 * more than before) when it's whole, so a reader copies, then checks that it
 * saw the same even number before and after. */
#define SHM_MAGIC 0x47425342u /* "GBSB" */

typedef struct {
	uint32_t magic, w, h, stride;
	volatile uint64_t seq;
	uint32_t pid, pad;
} ShmHeader;

static void shm_path(char* out, size_t n) {
	const char* e = getenv("GBADS_SECOND_SHM");
	if (e && *e) {
		snprintf(out, n, "%s", e);
		return;
	}
	const char* dir = getenv("XDG_RUNTIME_DIR");
	snprintf(out, n, "%s/gbads-second.shm", dir && *dir ? dir : "/tmp");
}

bool second_shm_parse_size(const char* s, int* w, int* h) {
	int a = 0, b = 0;
	char x;
	if (!s || sscanf(s, "%d%c%d", &a, &x, &b) != 3 || (x != 'x' && x != 'X') || a < 160 || b < 160 || a > 8192 || b > 8192) return false;
	*w = a, *h = b;
	return true;
}

/* ------------------------------------------------------------ the game */

static ShmHeader* pub;
static size_t pubSize;

bool second_shm_open(int w, int h) {
	char path[300];
	shm_path(path, sizeof(path));
	int fd = open(path, O_RDWR | O_CREAT, 0600);
	if (fd < 0) {
		fprintf(stderr, "second screen: can't open %s\n", path);
		return false;
	}
	size_t size = sizeof(ShmHeader) + (size_t) w * h * 4;
	if (ftruncate(fd, (off_t) size) != 0) {
		close(fd);
		fprintf(stderr, "second screen: can't size %s\n", path);
		return false;
	}
	void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (p == MAP_FAILED) {
		fprintf(stderr, "second screen: can't map %s\n", path);
		return false;
	}
	pub = p;
	pubSize = size;
	pub->w = (uint32_t) w, pub->h = (uint32_t) h, pub->stride = (uint32_t) w * 4, pub->pid = (uint32_t) getpid();
	memset(pub + 1, 0, (size_t) w * h * 4);
	pub->seq = 0;
	__atomic_store_n(&pub->magic, SHM_MAGIC, __ATOMIC_RELEASE);
	fprintf(stderr, "second screen: %dx%d, published to %s\n", w, h, path);
	return true;
}

void second_shm_publish(const SDL_Surface* f) {
	if (!pub || !f || !f->pixels || f->w != (int) pub->w || f->h != (int) pub->h) return;
	uint64_t s = pub->seq;
	__atomic_store_n(&pub->seq, s + 1, __ATOMIC_RELEASE); /* odd: being written */
	uint8_t* dst = (uint8_t*) (pub + 1);
	for (int y = 0; y < f->h; ++y) memcpy(dst + (size_t) y * pub->stride, (const uint8_t*) f->pixels + (size_t) y * f->pitch, pub->stride);
	__atomic_store_n(&pub->seq, s + 2, __ATOMIC_RELEASE);
}

void second_shm_close(void) {
	if (!pub) return;
	__atomic_store_n(&pub->magic, 0, __ATOMIC_RELEASE); /* the presenter sees the game is gone */
	munmap(pub, pubSize);
	pub = NULL;
}

/* -------------------------------------------------------- the presenter */

/* Shows the shared frame full screen on the display it's started on, kept in
 * proportion (black bars where the shapes differ). It waits for the game to
 * appear, follows it if it starts again, and shows black while there's no game.
 * No input is sent back yet: the panel's touches don't reach the game. */
int second_shm_present_main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (!strncmp(argv[i], "--second-shm=", 13)) setenv("GBADS_SECOND_SHM", argv[i] + 13, 1);
	}
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "present-second: SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_ShowCursor(SDL_DISABLE);
	SDL_Window* win = SDL_CreateWindow("GBA DS second screen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1240, 1080,
	                                   SDL_WINDOW_RESIZABLE | (getenv("GBADS_WINDOWED") ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP));
	SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
	if (win && !ren) ren = SDL_CreateRenderer(win, -1, 0);
	if (!ren) {
		fprintf(stderr, "present-second: no window or renderer: %s\n", SDL_GetError());
		return 1;
	}
	char path[300];
	shm_path(path, sizeof(path));
	fprintf(stderr, "present-second: waiting for %s\n", path);

	ShmHeader* hdr = NULL;
	size_t mapSize = 0;
	SDL_Texture* tex = NULL;
	int tw = 0, th = 0;
	uint8_t* copy = NULL;
	uint64_t shown = ~0ull;
	bool quit = false, haveFrame = false;
	while (!quit) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && (e.key.keysym.mod & KMOD_CTRL))) quit = true;
		}
		if (hdr && hdr->magic != SHM_MAGIC) { /* the game closed it */
			munmap(hdr, mapSize);
			hdr = NULL;
			haveFrame = false;
			shown = ~0ull;
		}
		if (!hdr) {
			int fd = open(path, O_RDWR);
			struct stat st;
			if (fd >= 0 && fstat(fd, &st) == 0 && (size_t) st.st_size > sizeof(ShmHeader)) {
				void* p = mmap(NULL, (size_t) st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
				if (p != MAP_FAILED) {
					ShmHeader* h = p;
					if (h->magic == SHM_MAGIC && sizeof(ShmHeader) + (size_t) h->w * h->h * 4 <= (size_t) st.st_size) {
						hdr = h, mapSize = (size_t) st.st_size;
						fprintf(stderr, "present-second: the game's second screen, %ux%u\n", h->w, h->h);
					} else {
						munmap(p, (size_t) st.st_size);
					}
				}
			}
			if (fd >= 0) close(fd);
		}
		if (hdr) {
			int w = (int) hdr->w, h = (int) hdr->h;
			if (!tex || tw != w || th != h) {
				if (tex) SDL_DestroyTexture(tex);
				free(copy);
				tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
				copy = malloc((size_t) w * h * 4);
				tw = w, th = h;
				shown = ~0ull;
			}
			uint64_t s0 = __atomic_load_n(&hdr->seq, __ATOMIC_ACQUIRE);
			if (tex && copy && s0 != shown && !(s0 & 1)) {
				memcpy(copy, hdr + 1, (size_t) w * h * 4);
				uint64_t s1 = __atomic_load_n(&hdr->seq, __ATOMIC_ACQUIRE);
				if (s1 == s0) { /* whole: nothing was written meanwhile */
					SDL_UpdateTexture(tex, NULL, copy, w * 4);
					shown = s0;
					haveFrame = true;
				}
			}
		}
		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);
		if (haveFrame && tex) {
			int ow, oh;
			SDL_GetRendererOutputSize(ren, &ow, &oh);
			float k = (float) ow / tw < (float) oh / th ? (float) ow / tw : (float) oh / th;
			SDL_Rect r = { 0, 0, (int) (tw * k), (int) (th * k) };
			r.x = (ow - r.w) / 2, r.y = (oh - r.h) / 2;
			SDL_RenderCopy(ren, tex, NULL, &r);
		}
		SDL_RenderPresent(ren);
		if (!hdr || !haveFrame) SDL_Delay(100);
		else SDL_Delay(2); /* (vsync paces it; this keeps a core free if it's off) */
	}
	if (tex) SDL_DestroyTexture(tex);
	free(copy);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}

#else /* Android has its own second screen, the web build no files to share */
bool second_shm_open(int w, int h) { (void) w, (void) h; return false; }
void second_shm_publish(const SDL_Surface* f) { (void) f; }
void second_shm_close(void) {}
bool second_shm_parse_size(const char* s, int* w, int* h) { (void) s, (void) w, (void) h; return false; }
int second_shm_present_main(int argc, char** argv) { (void) argc, (void) argv; return 1; }
#endif
