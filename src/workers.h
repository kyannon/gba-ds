/* A small pool of worker threads, one per slot: run a function on slot i, wait
 * for it, stop them all. Where a thread can't be made (the web build), the
 * function runs on the caller at once. No SDL: a libretro core can use it too
 * (or put its own threads behind these three calls). */
#ifndef WORKERS_H
#define WORKERS_H

#define WORKER_SLOTS 2

/* fn(arg) on worker i (started the first time it's asked); no thread: here, now */
void worker_start(int i, void (*fn)(void*), void* arg);
/* returns once worker i is idle */
void worker_wait(int i);
/* stops and joins every worker that was started */
void worker_stop_all(void);

#endif
