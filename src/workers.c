#include "workers.h"
#include <pthread.h>
#include <stdbool.h>

/* a worker thread for each slot: runs fn(arg) when asked */
static struct {
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started, busy, quit;
	void (*fn)(void*);
	void* arg;
} work[WORKER_SLOTS] = { { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER },
                         { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER } };

static void* worker_main(void* arg) {
	typeof(work[0])* w = arg;
	pthread_mutex_lock(&w->lock);
	for (;;) {
		while (!w->busy && !w->quit) pthread_cond_wait(&w->cond, &w->lock);
		if (w->quit) break;
		void (*fn)(void*) = w->fn;
		void* a = w->arg;
		pthread_mutex_unlock(&w->lock);
		fn(a);
		pthread_mutex_lock(&w->lock);
		w->busy = false;
		pthread_cond_broadcast(&w->cond);
	}
	pthread_mutex_unlock(&w->lock);
	return NULL;
}

void worker_start(int i, void (*fn)(void*), void* arg) {
	typeof(work[0])* w = &work[i];
	pthread_mutex_lock(&w->lock);
	if (!w->started) {
		w->quit = false;
		w->started = pthread_create(&w->thread, NULL, worker_main, w) == 0;
	}
	bool ok = w->started;
	if (ok) w->fn = fn, w->arg = arg, w->busy = true, pthread_cond_broadcast(&w->cond);
	pthread_mutex_unlock(&w->lock);
	if (!ok) fn(arg);
}

void worker_wait(int i) {
	typeof(work[0])* w = &work[i];
	pthread_mutex_lock(&w->lock);
	while (w->busy) pthread_cond_wait(&w->cond, &w->lock);
	pthread_mutex_unlock(&w->lock);
}

void worker_stop_all(void) {
	for (int i = 0; i < WORKER_SLOTS; ++i) {
		typeof(work[0])* w = &work[i];
		pthread_mutex_lock(&w->lock);
		bool started = w->started;
		w->quit = true;
		pthread_cond_broadcast(&w->cond);
		pthread_mutex_unlock(&w->lock);
		if (started) pthread_join(w->thread, NULL);
		w->started = false;
	}
}
