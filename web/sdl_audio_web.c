/* SDL's audio driver for GBA DS's web build: the device has no thread of
 * its own (the build has none); the page pulls the queued sound out with
 * web_audio_pull as fast as its speakers play it, so SDL's queue fills and
 * drains in real time as it does on a device, and game.c's pacing (the queue
 * kept near audio_latency) works unchanged. Pulled: 16-bit stereo at the rate
 * web_audio_rate gives (what game.c asks for, 48000 Hz).
 *
 * SDL2 has no slot for a driver of one's own, so this one takes the
 * Emscripten driver's (SDL_AUDIO_DRIVER_EMSCRIPTEN in SDL_config_wasi.h;
 * SDL's own Emscripten file isn't compiled). */
#include "../deps/SDL2/src/SDL_internal.h"

#include "SDL_audio.h"
#include "SDL_timer.h"
#include "../deps/SDL2/src/audio/SDL_sysaudio.h"

static SDL_AudioDevice* webDevice;

static int WEBAUDIO_OpenDevice(SDL_AudioDevice* this, const char* devname) {
	(void) devname;
	if (webDevice) return SDL_SetError("the web build has one audio device");
	this->hidden = (struct SDL_PrivateAudioData*) SDL_calloc(1, 1);
	if (!this->hidden) return SDL_OutOfMemory();
	/* whatever was asked for is what the page gets: no conversion here */
	SDL_CalculateAudioSpec(&this->spec);
	webDevice = this;
	return 0;
}

static void WEBAUDIO_CloseDevice(SDL_AudioDevice* this) {
	if (webDevice == this) webDevice = NULL;
	SDL_free(this->hidden);
	this->hidden = NULL;
}

static SDL_bool WEBAUDIO_Init(SDL_AudioDriverImpl* impl) {
	impl->OpenDevice = WEBAUDIO_OpenDevice;
	impl->CloseDevice = WEBAUDIO_CloseDevice;
	impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
	impl->ProvidesOwnCallbackThread = SDL_TRUE; /* the page */
	impl->SupportsNonPow2Samples = SDL_TRUE;
	return SDL_TRUE;
}

AudioBootStrap EMSCRIPTENAUDIO_bootstrap = { "web", "GBA DS web page", WEBAUDIO_Init, SDL_FALSE };

/* Up to `bytes` of the queued sound into `out`; how many it gave (0 while
 * the device is paused, e.g. in the pause menu, or nothing is queued). */
__attribute__((export_name("web_audio_pull"))) int web_audio_pull(void* out, int bytes) {
	SDL_AudioDevice* d = webDevice;
	if (!d || !SDL_AtomicGet(&d->enabled) || SDL_AtomicGet(&d->paused) || bytes <= 0) return 0;
	int have = (int) SDL_GetQueuedAudioSize(d->id);
	int n = (have < bytes ? have : bytes) & ~3; /* whole sample frames */
	if (n <= 0) return 0;
	d->callbackspec.callback(d->callbackspec.userdata, (Uint8*) out, n);
	return n;
}

/* The pulled sound's rate (Hz), and 0 while no device is open. */
__attribute__((export_name("web_audio_rate"))) int web_audio_rate(void) {
	return webDevice ? webDevice->spec.freq : 0;
}
