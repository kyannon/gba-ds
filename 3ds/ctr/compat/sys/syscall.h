/* Linux's thread calls as the app makes them (a background thread lowering
 * its own priority, a thread's CPU time): nothing on the 3DS, where host.c
 * gives the app's threads their priority and core. */
#ifndef GBADS_CTR_SYSCALL_H
#define GBADS_CTR_SYSCALL_H
#include <time.h>
#define SYS_gettid 0
#define syscall(n) 0
#ifndef PRIO_PROCESS
#define PRIO_PROCESS 0
#define setpriority(which, who, prio) ((void) (who), 0)
#endif
#ifndef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID CLOCK_MONOTONIC
#endif
#endif
