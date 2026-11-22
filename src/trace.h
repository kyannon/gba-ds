/* Diagnostic timeline and system sampler (issue #36, GammaOS performance).
 *
 * What it gives a log (Android: gba-ds.log and logcat tag GBADS; elsewhere
 * stderr, with GBADS_TRACE=1):
 *   - trace(): a line stamped "[+12345.6]", the milliseconds since the
 *     process started (the same clock as the Java side's launch.log
 *     stamps, so the two files line up);
 *   - trace_since(): "what took X ms" for a phase;
 *   - named stats (TRACE_ADD): averages, minimum and maximum of a number
 *     over each second, printed as "stats[group]" lines;
 *   - a sampler thread (low priority) that logs once a second (a quarter of a
 *     second around start-up, loading and shutdown) how busy each core is, its clock, which of
 *     our threads ran, memory, I/O, pressure, temperatures: whatever the
 *     device lets an app read. What isn't readable is said once, at start.
 *
 * Android: on unless GBA DS/trace-off exists. Elsewhere: GBADS_TRACE=1. */
#ifndef GBADS_TRACE_H
#define GBADS_TRACE_H
#include <stdbool.h>

#if defined(__linux__) && !defined(__wasi__)

extern bool traceOn;

/* first thing in main(): the clock, and whether tracing is on */
void trace_init(void);
static inline bool trace_enabled(void) { return traceOn; }
/* milliseconds since the process started */
double trace_ms(void);
void trace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
/* "WHAT: X ms" with the stamp; t0 is a trace_ms() from the phase's start. Returns now. */
double trace_since(const char* what, double t0);
/* names the calling thread (shows in the sampler's list; 15 letters) */
void trace_thread(const char* name);
/* what the app is doing now, shown in every sampler line ("library", "game", "menu", "exit"...);
 * a change also speeds the sampler up for 15 s */
void trace_phase(const char* name);

int trace_stat(const char* name); /* "group.name" */
void trace_add(int id, double value);
#define TRACE_ADD(name, value) \
	do { \
		if (traceOn) { \
			static int _id = -2; \
			if (_id == -2) _id = trace_stat(name); \
			trace_add(_id, (value)); \
		} \
	} while (0)

/* once the platform's logging is up (Android: after android_init_log): the
 * device and system facts that matter, then the sampler thread */
void trace_start(void);
void trace_snapshot(const char* why);

#else

#define traceOn 0
static inline void trace_init(void) {}
static inline bool trace_enabled(void) { return false; }
static inline double trace_ms(void) { return 0; }
#define trace(...) ((void) 0)
static inline double trace_since(const char* w, double t0) { (void) w; return t0; }
static inline void trace_thread(const char* n) { (void) n; }
static inline void trace_phase(const char* n) { (void) n; }
#define TRACE_ADD(name, value) ((void) 0)
static inline int trace_stat(const char* n) { (void) n; return -1; }
static inline void trace_add(int id, double v) { (void) id; (void) v; }
static inline void trace_start(void) {}
static inline void trace_snapshot(const char* why) { (void) why; }

#endif
#endif
