/* uname for the app's log line: the 3DS has no such call. */
#ifndef GBADS_CTR_UTSNAME_H
#define GBADS_CTR_UTSNAME_H
#include <string.h>
#include <sys/syscall.h>
struct utsname {
	char sysname[32], nodename[32], release[32], version[32], machine[32];
};
static inline int uname(struct utsname* u) {
	memset(u, 0, sizeof(*u));
	strcpy(u->sysname, "Horizon");
	strcpy(u->machine, "armv6k");
	return 0;
}
#endif
